//
// $id: SkeletonBinary.c https://github.com/zhongfq/spine-binaryreader $
//

#include "SkeletonBinary.h"
#include "spine/extension.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#define BONE_ROTATE     0
#define BONE_TRANSLATE  1
#define BONE_SCALE      2
#define BONE_SHEAR      3

#define SLOT_ATTACHMENT 0
#define SLOT_COLOR      1

#define PATH_POSITION   0
#define PATH_SPACING    1
#define PATH_MIX        2

#define CURVE_LINEAR    0
#define CURVE_STEPPED   1
#define CURVE_BEZIER    2

#define READ() (((int)self->data->content[self->data->position++]) & 0xFF)

typedef struct {
    const char* parent;
    const char* skin;
    int slotIndex;
    spMeshAttachment* mesh;
} _spLinkedMesh;

typedef struct {
    int count;
    int capacity;
    spTimeline **timelines;
} _spTimelineArray;

typedef struct _spStringBuffer {
    struct _spStringBuffer *next;
    int position;
    int capacity;
    char *content;
} _spStringBuffer;

typedef struct {
    float scale;
    
    _spStringBuffer *data;
    
    _spStringBuffer *buffer;
    
    int linkedMeshCount;
    int linkedMeshCapacity;
    _spLinkedMesh* linkedMeshes;
    
    // don't need free
    spSkeletonData *skeletonData;
    spAttachmentLoader *attachmentLoader;
} spSkeletonBinary;

static inline bool readBoolean(spSkeletonBinary *self)
{
    int ch = READ();
    return ch != 0;
}

static inline char readByte(spSkeletonBinary *self)
{
    int ch = READ();
    return (char)(ch);
}

static inline short readShort(spSkeletonBinary *self)
{
    int ch1 = READ();
    int ch2 = READ();
    return (short)((ch1 << 8) + (ch2 << 0));
}

static inline int readInt(spSkeletonBinary *self)
{
    int ch1 = READ();
    int ch2 = READ();
    int ch3 = READ();
    int ch4 = READ();
    return ((ch1 << 24) | (ch2 << 16) | (ch3 << 8) | (ch4 << 0));
}

static inline int readVarint(spSkeletonBinary *self, bool optimizePositive)
{
    int b = READ();
    unsigned int result = b & 0x7F;
    if ((b & 0x80) != 0) {
        b = READ();
        result |= (b & 0x7F) << 7;
        if ((b & 0x80) != 0) {
            b = READ();
            result |= (b & 0x7F) << 14;
            if ((b & 0x80) != 0) {
                b = READ();
                result |= (b & 0x7F) << 21;
                if ((b & 0x80) != 0) {
                    b = READ();
                    result |= (b & 0x7F) << 28;
                }
            }
        }
    }
    return (int)(optimizePositive ? result : ((result >> 1) ^ -(result & 1)));
}

static inline float readFloat(spSkeletonBinary *self)
{
    union {
        float f;
        int i;
    } u;

    u.i = readInt(self);
    return u.f;
}

static inline char *readString(spSkeletonBinary *self)
{
    char *current, *start;
    _spStringBuffer *buffer = self->buffer;
    int byteCount = readVarint(self, true);
 
    if (byteCount == 0) {
        return NULL;
    }
    
    if (buffer == NULL || buffer->capacity - buffer->position < byteCount) {
        self->buffer = (_spStringBuffer *)malloc(sizeof(_spStringBuffer));
        self->buffer->position = 0;
        self->buffer->capacity = MAX(BUFSIZ * 2, byteCount);
        self->buffer->content = (char *)malloc(self->buffer->capacity);
        self->buffer->next = buffer;
        buffer = self->buffer;
    }
    
    start = buffer->content + buffer->position;
    current = start;
    byteCount--;
    for (int i = 0; i < byteCount;) {
        int b = READ();
        switch (b >> 4) {
            case -1:
                break;
            case 12:
            case 13: {
                int b2 = READ();
                *current++ = (char)((b & 0x1F) << 6 | (b2 & 0x3F));
                i += 2;
                break;
            }
            case 14: {
                int b2 = READ();
                int b3 = READ();
                *current++ = (char)((b & 0x0F) << 12 | (b2 & 0x3F) << 6 | (b3 & 0x3F));
                i += 3;
                break;
            }
            default: {
                *current++ = (char)b;
                i++;
            }
        }
    }
    
    *current++ = '\0';
    buffer->position += (int)(current - start);
    
    return start;
}

static const char *copyString(const char *src) {
    if (src == NULL) {
        return NULL;
    }
    
    const char *dest;
    MALLOC_STR(dest, src);
    return dest;
}

static inline float *readFloats(spSkeletonBinary *self, float scale, size_t length)
{
    float *arr = MALLOC(float, length);
    for (int i = 0; i < length; i++)
    {
        arr[i] = readFloat(self) * scale;
    }

    return arr;
}

static inline unsigned short *readShorts(spSkeletonBinary *self, size_t length)
{
    unsigned short *arr = MALLOC(unsigned short, length);
    for (int i = 0; i < length; i++)
    {
        arr[i] = readShort(self);
    }

    return arr;
}

static inline void readColor(spSkeletonBinary *self, float *r, float *g, float *b, float *a)
{
    *r = READ() / (float)255;
    *g = READ() / (float)255;
    *b = READ() / (float)255;
    *a = READ() / (float)255;
}

static void addLinkedMesh (spSkeletonBinary* self, spMeshAttachment* mesh, const char* skin, int slotIndex, const char* parent) {
    _spLinkedMesh* linkedMesh;
    
    if (self->linkedMeshCapacity == 0 || self->linkedMeshCount == self->linkedMeshCapacity) {
        self->linkedMeshCapacity = self->linkedMeshCapacity == 0 ? 8 : self->linkedMeshCapacity * 2;
        self->linkedMeshes = (_spLinkedMesh *)realloc(self->linkedMeshes, sizeof(_spLinkedMesh) * self->linkedMeshCapacity);
    }
    
    linkedMesh = self->linkedMeshes + self->linkedMeshCount++;
    linkedMesh->mesh = mesh;
    linkedMesh->skin = skin;
    linkedMesh->slotIndex = slotIndex;
    linkedMesh->parent = parent;
}

static void readVertices(spSkeletonBinary *self, spVertexAttachment *attachment, int vertexCount)
{
    if (!readBoolean(self)) {
        attachment->vertices = readFloats(self, self->scale, vertexCount << 1);
        attachment->verticesCount = vertexCount << 1;
        attachment->bones = NULL;
        attachment->bonesCount = 0;
    } else {
        float *weights;
        int *bones;
        int weightCount = 0, boneCount = 0;
        int position = self->data->position;
        
        for (int i = 0; i < vertexCount; i++) {
            int nn = readVarint(self, true);
            boneCount++;
            for (int ii = 0; ii < nn; ii++) {
                readVarint(self, true);
                self->data->position += sizeof(float) * 3;
                weightCount += 3;
                boneCount++;
            }
        }
        
        self->data->position = position;
        
        attachment->bones = MALLOC(int, boneCount);
        attachment->bonesCount = boneCount;
        attachment->vertices = MALLOC(float, weightCount);
        attachment->verticesCount = weightCount;
        weights = attachment->vertices;
        bones = attachment->bones;
        
        for (int i = 0; i < vertexCount; i++) {
            int nn = readVarint(self, true);
            *bones++ = nn;
            for (int ii = 0; ii < nn; ii++) {
                *bones++ = readVarint(self, true);
                *weights++ = readFloat(self) * self->scale;
                *weights++ = readFloat(self) * self->scale;
                *weights++ = readFloat(self);
            }
        }
    }
}

static spAttachment *readAttachment(spSkeletonBinary *self, spSkin *skin, int slotIndex, const char *attachmentName)
{
    spAttachment *attachment = NULL;
    float scale = self->scale;
    
    char *name = readString(self);
    if (name == NULL) name = (char *)attachmentName;
    
    switch ((spAttachmentType)readByte(self)) {
        case SP_ATTACHMENT_REGION: {
            spRegionAttachment *region;
            
            char *path = readString(self);
            if (path == NULL) path = name;
            
            attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, skin, SP_ATTACHMENT_REGION, name, path);
            region = SUB_CAST(spRegionAttachment, attachment);
            if (path) {
                region->path = copyString(path);
            }
            
            region->rotation = readFloat(self);
            region->x = readFloat(self) * scale;
            region->y = readFloat(self) * scale;
            region->scaleX = readFloat(self);
            region->scaleY = readFloat(self);
            region->width = readFloat(self) * scale;
            region->height = readFloat(self) * scale;
            readColor(self, &region->r, &region->g, &region->b, &region->a);
            
            spRegionAttachment_updateOffset(region);
            spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);
            break;
        }
        case SP_ATTACHMENT_BOUNDING_BOX: {
            spBoundingBoxAttachment *boundingBox;
            int vertexCount = readVarint(self, true);
            
            attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, skin, SP_ATTACHMENT_BOUNDING_BOX, name, name);
            boundingBox = SUB_CAST(spBoundingBoxAttachment, attachment);
            readVertices(self, SUPER(boundingBox), vertexCount);
            SUPER(boundingBox)->worldVerticesLength = vertexCount << 1;
            
            spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);
            
            break;
        }
        case SP_ATTACHMENT_MESH: {
            spMeshAttachment *mesh;
            int vertexCount;
            
            char *path = readString(self);
            if (path == NULL) path = name;
            
            attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, skin, SP_ATTACHMENT_MESH, name, path);
            mesh = SUB_CAST(spMeshAttachment, attachment);
            if (path) {
                mesh->path = copyString(path);
            }
            
            readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
            vertexCount = readVarint(self, true);
            mesh->regionUVs = readFloats(self, 1, vertexCount << 1);
            mesh->trianglesCount = readVarint(self, true);
            mesh->triangles = readShorts(self, mesh->trianglesCount);
            readVertices(self, SUPER(mesh), vertexCount);
            SUPER(mesh)->worldVerticesLength = vertexCount << 1;
            mesh->hullLength = readVarint(self, true) << 1;
            
            spMeshAttachment_updateUVs(mesh);
            spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);
            break;
        }
        case SP_ATTACHMENT_LINKED_MESH: {
            spMeshAttachment *mesh;
            
            char *parent;
            char *skinName;
            char *path = readString(self);
            if (path == NULL) path = name;
            
            attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, skin, SP_ATTACHMENT_LINKED_MESH, name, path);
            mesh = SUB_CAST(spMeshAttachment, attachment);
            if (path) {
                mesh->path = copyString(path);
            }
            
            readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
            skinName = readString(self);
            parent = readString(self);
            mesh->inheritDeform = readBoolean(self);
            
            addLinkedMesh(self, mesh, skinName, slotIndex, parent);
            break;
        }
        case SP_ATTACHMENT_PATH: {
            spPathAttachment *path;
            int vertexCount;
            
            attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, skin, SP_ATTACHMENT_PATH, name, NULL);
            path = SUB_CAST(spPathAttachment, attachment);
            
            path->closed = readBoolean(self);
            path->constantSpeed = readBoolean(self);
            vertexCount = readVarint(self, true);
            readVertices(self, SUPER(path), vertexCount);
            SUPER(path)->worldVerticesLength = vertexCount << 1;
            path->lengthsLength = vertexCount / 3;
            path->lengths = MALLOC(float, path->lengthsLength);
            for (int i = 0; i < path->lengthsLength; i++) {
                path->lengths[i] = readFloat(self) * self->scale;
            }
            
            break;
        }
    }
    
    return attachment;
}

static spSkin *readSkin(spSkeletonBinary *self, const char *skinName)
{
    spSkin *skin;
    int slotCount = readVarint(self, true);
    
    if (slotCount == 0) {
        return NULL;
    }
    
    skin = spSkin_create(skinName);
    for (int i = 0; i < slotCount; i++) {
        int slotIndex = readVarint(self, true);
        int nn = readVarint(self, true);
        for (int ii = 0; ii < nn; ii++) {
            char *name = readString(self);
            spAttachment *attachment = readAttachment(self, skin, slotIndex, name);
            spSkin_addAttachment(skin, slotIndex, name, attachment);
        }
    }
    
    return skin;
}

static void readCurve(spSkeletonBinary *self, spCurveTimeline *timeline, int frameIndex)
{
    switch (readByte(self)) {
        case CURVE_STEPPED: {
            spCurveTimeline_setStepped(timeline, frameIndex);
            break;
        }
        case CURVE_BEZIER: {
            float v1 = readFloat(self);
            float v2 = readFloat(self);
            float v3 = readFloat(self);
            float v4 = readFloat(self);
            spCurveTimeline_setCurve(timeline, frameIndex, v1, v2, v3, v4);
            break;
        }
    }
}

static void addAnimationTimeline(_spTimelineArray *arr, spTimeline *timeline)
{
    if (arr->capacity == 0 || arr->capacity == arr->count) {
        arr->capacity = arr->capacity == 0 ? 32 : arr->capacity * 2;
        arr->timelines = (spTimeline **)realloc(arr->timelines, arr->capacity * sizeof(spTimeline *));
    }
    arr->timelines[arr->count++] = timeline;
}

static spAnimation *readAnimation(spSkeletonBinary *self, const char *name)
{
    float scale = self->scale;
    float duration = 0;
    int drawOrderCount;
    int eventCount;
    
    spAnimation *animation;
    _spTimelineArray arr;
    arr.timelines = NULL;
    arr.capacity = 0;
    arr.count = 0;
    
    // slot timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        int slotIndex = readVarint(self, true);
        for (int ii = 0, nn = readVarint(self, true); ii < nn; ii++) {
            int timelineType = readByte(self);
            int frameCount = readVarint(self, true);
            switch (timelineType) {
                case SLOT_COLOR: {
                    spColorTimeline *timeline = spColorTimeline_create(frameCount);
                    timeline->slotIndex = slotIndex;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        float r, g, b, a;
                        readColor(self, &r, &g, &b, &a);
                        spColorTimeline_setFrame(timeline, frameIndex, time, r, g, b, a);
                        if (frameIndex < frameCount - 1) {
                            readCurve(self, SUPER(timeline), frameIndex);
                        }
                    }
                    duration = MAX(duration, timeline->frames[(frameCount - 1) * COLOR_ENTRIES]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
                case SLOT_ATTACHMENT: {
                    spAttachmentTimeline *timeline = spAttachmentTimeline_create(frameCount);
                    timeline->slotIndex = slotIndex;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        char *name = readString(self);
                        spAttachmentTimeline_setFrame(timeline, frameIndex, time, name);
                    }
                    duration = MAX(duration, timeline->frames[frameCount - 1]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
            }
        }
    }
    
    // bone timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        int boneIndex = readVarint(self, true);
        for (int ii = 0, nn = readVarint(self, true); ii < nn; ii++) {
            int timelineType = readByte(self);
            int frameCount = readVarint(self, true);
            switch (timelineType) {
                case BONE_ROTATE: {
                    spRotateTimeline *timeline = spRotateTimeline_create(frameCount);
                    timeline->boneIndex = boneIndex;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        float angle = readFloat(self);
                        spRotateTimeline_setFrame(timeline, frameIndex, time, angle);
                        if (frameIndex < frameCount - 1) {
                            readCurve(self, SUPER(timeline), frameIndex);
                        }
                    }
                    
                    duration = MAX(duration, timeline->frames[(frameCount - 1) * ROTATE_ENTRIES]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
                case BONE_TRANSLATE:
                case BONE_SCALE:
                case BONE_SHEAR: {
                    spTranslateTimeline *timeline;
                    float timelineScale = 1;
                    if (timelineType == BONE_SCALE) {
                        timeline = spScaleTimeline_create(frameCount);
                    }
                    else if (timelineType == BONE_SHEAR)
                        timeline = spShearTimeline_create(frameCount);
                    else {
                        timeline = spTranslateTimeline_create(frameCount);
                        timelineScale = scale;
                    }
                    timeline->boneIndex = boneIndex;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        float x = readFloat(self) * timelineScale;
                        float y = readFloat(self) * timelineScale;
                        spTranslateTimeline_setFrame(timeline, frameIndex, time, x, y);
                        if (frameIndex < frameCount - 1) {
                            readCurve(self, SUPER(timeline), frameIndex);
                        }
                    }
                    
                    duration = MAX(duration, timeline->frames[(frameCount - 1) * TRANSLATE_ENTRIES]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
            }
        }
    }
    
    // ik constraint timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        int index = readVarint(self, true);
        int frameCount = readVarint(self, true);
        spIkConstraintTimeline *timeline = spIkConstraintTimeline_create(frameCount);
        timeline->ikConstraintIndex = index;
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            float time = readFloat(self);
            float mix = readFloat(self);
            int blendPositive = readByte(self);
            spIkConstraintTimeline_setFrame(timeline, frameIndex, time, mix, blendPositive);
            if (frameIndex < frameCount - 1) {
                readCurve(self, SUPER(timeline), frameIndex);
            }
        }
        duration = MAX(duration, timeline->frames[(frameCount - 1) * IKCONSTRAINT_ENTRIES]);
        addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
    }
    
    // transform constraint timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        int index = readVarint(self, true);
        int frameCount = readVarint(self, true);
        spTransformConstraintTimeline *timeline = spTransformConstraintTimeline_create(frameCount);
        timeline->transformConstraintIndex = index;
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            float time = readFloat(self);
            float rotateMix = readFloat(self);
            float translateMix = readFloat(self);
            float scaleMix = readFloat(self);
            float shearMix = readFloat(self);
            spTransformConstraintTimeline_setFrame(timeline, frameIndex, time, rotateMix, translateMix, scaleMix, shearMix);
            if (frameIndex < frameCount - 1) {
                readCurve(self, SUPER(timeline), frameIndex);
            }
        }
        
        duration = MAX(duration, timeline->frames[(frameCount - 1) * TRANSFORMCONSTRAINT_ENTRIES]);
        addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
    }
    
    // path constraint timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        int index = readVarint(self, true);
        spPathConstraintData *data = self->skeletonData->pathConstraints[index];
        for (int ii = 0, nn = readVarint(self, true); ii < nn; ii++) {
            int timelineType = readByte(self);
            int frameCount = readVarint(self, true);
            switch (timelineType) {
                case PATH_POSITION:
                case PATH_SPACING: {
                    spPathConstraintPositionTimeline *timeline;
                    float timelineScale = 1;
                    if (timelineType == PATH_SPACING) {
                        timeline = (spPathConstraintPositionTimeline *)spPathConstraintSpacingTimeline_create(frameCount);
                        if (data->spacingMode == SP_SPACING_MODE_LENGTH || data->spacingMode == SP_SPACING_MODE_FIXED) {
                            timelineScale = scale;
                        }
                    } else {
                        timeline = spPathConstraintPositionTimeline_create(frameCount);
                        if (data->positionMode == SP_POSITION_MODE_FIXED) {
                            timelineScale = scale;
                        }
                    }
                    timeline->pathConstraintIndex = index;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        float value = readFloat(self);
                        spPathConstraintPositionTimeline_setFrame(timeline, frameIndex, time, value);
                        if (frameIndex < frameCount - 1) {
                            readCurve(self, SUPER(timeline), frameIndex);
                        }
                    }
                    duration = MAX(duration, timeline->frames[(frameCount - 1) * PATHCONSTRAINTPOSITION_ENTRIES]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
                case PATH_MIX: {
                    spPathConstraintMixTimeline *timeline = spPathConstraintMixTimeline_create(frameCount);
                    timeline->pathConstraintIndex = index;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        float time = readFloat(self);
                        float rotateMix = readFloat(self);
                        float translateMix = readFloat(self);
                        spPathConstraintMixTimeline_setFrame(timeline, frameIndex, time, rotateMix, translateMix);
                        if (frameIndex < frameCount - 1) {
                            readCurve(self, SUPER(timeline), frameIndex);
                        }
                    }
                    duration = MAX(duration, timeline->frames[(frameCount - 1) * PATHCONSTRAINTMIX_ENTRIES]);
                    addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
                    break;
                }
            }
        }
    }
    
    // deform timelines
    for (int i = 0, n = readVarint(self, true); i < n; i++) {
        spSkin *skin = self->skeletonData->skins[readVarint(self, true)];
        for (int ii = 0, nn = readVarint(self, true); ii < nn; ii++) {
            int slotIndex = readVarint(self, true);
            for (int iii = 0, nnn = readVarint(self, true); iii < nnn; iii++) {
                char *name = readString(self);
                spVertexAttachment *attachment = SUB_CAST(spVertexAttachment, spSkin_getAttachment(skin, slotIndex, name));
                bool weighted = attachment->bones != NULL;
                float *vertices = attachment->vertices;
                int deformLength = weighted ? attachment->verticesCount / 3 * 2 : attachment->verticesCount;
                
                int frameCount = readVarint(self, true);
                
                spDeformTimeline *timeline = spDeformTimeline_create(frameCount, deformLength);
                timeline->slotIndex = slotIndex;
                timeline->attachment = SUPER(attachment);
                
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    float time = readFloat(self);
                    float *deform;
                    int end = readVarint(self, true);
                    if (end == 0)
                        deform = weighted ? MALLOC(float, deformLength) : vertices;
                    else {
                        deform = MALLOC(float, deformLength);
                        int start = readVarint(self, true);
                        end += start;
                        if (scale == 1) {
                            for (int v = start; v < end; v++)
                                deform[v] = readFloat(self);
                        } else {
                            for (int v = start; v < end; v++)
                                deform[v] = readFloat(self) * scale;
                        }
                        if (!weighted) {
                            for (int v = 0, vn = deformLength; v < vn; v++)
                                deform[v] += vertices[v];
                        }
                    }
                    spDeformTimeline_setFrame(timeline, frameIndex, time, deform);
                    if (frameIndex < frameCount - 1) {
                        readCurve(self, SUPER(timeline), frameIndex);
                    }
                }
                duration = MAX(duration, timeline->frames[frameCount - 1]);
                addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
            }
        }
    }
    
    // draw order timeline
    drawOrderCount = readVarint(self, true);
    if (drawOrderCount > 0) {
        int frameCount = drawOrderCount;
        int slotCount = self->skeletonData->slotsCount;
        spDrawOrderTimeline *timeline = spDrawOrderTimeline_create(frameCount, slotCount);
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            float time = readFloat(self);
            int offsetCount = readVarint(self, true);
            int *drawOrder = MALLOC(int, slotCount);
            for (int ii = slotCount - 1; ii >= 0; ii--) {
                drawOrder[ii] = -1;
            }
            int *unchanged = MALLOC(int, slotCount - offsetCount);
            int originalIndex = 0, unchangedIndex = 0;
            for (int ii = 0; ii < offsetCount; ii++) {
                int slotIndex = readVarint(self, true);
                // Collect unchanged items.
                while (originalIndex != slotIndex) {
                    unchanged[unchangedIndex++] = originalIndex++;
                }
                // Set changed items.
                drawOrder[originalIndex + readVarint(self, true)] = originalIndex;
                originalIndex++;
            }
            // Collect remaining unchanged items.
            while (originalIndex < slotCount) {
                unchanged[unchangedIndex++] = originalIndex++;
            }
            // Fill in unchanged items.
            for (int ii = slotCount - 1; ii >= 0; ii--) {
                if (drawOrder[ii] == -1) {
                    drawOrder[ii] = unchanged[--unchangedIndex];
                }
            }
            
            spDrawOrderTimeline_setFrame(timeline, frameIndex, time, drawOrder);
            FREE(drawOrder);
            FREE(unchanged);
        }
        duration = MAX(duration, timeline->frames[frameCount - 1]);
        addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
    }
    
    // event timeline
    eventCount = readVarint(self, true);
    if (eventCount > 0) {
        int frameCount = eventCount;
        spEventTimeline *timeline = spEventTimeline_create(frameCount);
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            float time = readFloat(self);
            spEventData *eventData = self->skeletonData->events[readVarint(self, true)];
            spEvent *event = spEvent_create(time, eventData);
            event->intValue = readVarint(self, false);
            event->floatValue = readFloat(self);
            event->stringValue = copyString(readBoolean(self) ? readString(self) : eventData->stringValue);
            spEventTimeline_setFrame(timeline, frameIndex, event);
        }
        duration = MAX(duration, timeline->frames[frameCount - 1]);
        addAnimationTimeline(&arr, SUPER_CAST(spTimeline, timeline));
    }
    
    animation = spAnimation_create(name, 0);
    FREE(animation->timelines);
    animation->timelinesCount = arr.count;
    animation->timelines = arr.timelines;
    animation->duration = duration;
    
    return animation;
}

static void readSkeleton(spSkeletonBinary *self)
{
    int length;
    float scale = self->scale;
    spSkeletonData* skeletonData = self->skeletonData;
    
    // header
    skeletonData->hash = copyString(readString(self));
    skeletonData->version = copyString(readString(self));
    skeletonData->width = readFloat(self);
    skeletonData->height = readFloat(self);
    readBoolean(self); // nonessential flag
    
    // bones
    length = readVarint(self, true);
    skeletonData->bones = MALLOC(spBoneData*, length);
    skeletonData->bonesCount = 0;
    for (int i = 0; i < length; i++) {
        char *name = readString(self);
        
        spBoneData *parent = i == 0 ? NULL : skeletonData->bones[readVarint(self, true)];
        spBoneData *data = spBoneData_create(i, name, parent);
        data->rotation = readFloat(self);
        data->x = readFloat(self) * scale;
        data->y = readFloat(self) * scale;
        data->scaleX = readFloat(self);
        data->scaleY = readFloat(self);
        data->shearX = readFloat(self);
        data->shearY = readFloat(self);
        data->length = readFloat(self) * scale;
        data->inheritRotation = readBoolean(self);
        data->inheritScale = readBoolean(self);
        skeletonData->bones[skeletonData->bonesCount++] = data;
    }
    
    // slots
    length = readVarint(self, true);
    skeletonData->slots = MALLOC(spSlotData *, length);
    skeletonData->slotsCount = 0;
    for (int i = 0; i < length; i++) {
        char *attachment;
        char *name = readString(self);
        
        spBoneData *boneData = skeletonData->bones[readVarint(self, true)];
        spSlotData *data = spSlotData_create(i, name, boneData);
        readColor(self, &data->r, &data->g, &data->b, &data->a);
        attachment = readString(self);
        spSlotData_setAttachmentName(data, attachment);
        data->blendMode = (spBlendMode)readByte(self);
        skeletonData->slots[skeletonData->slotsCount++] = data;
    }
    
    // ik constraints
    length = readVarint(self, true);
    skeletonData->ikConstraints = MALLOC(spIkConstraintData *, length);
    skeletonData->ikConstraintsCount = 0;
    for (int i = 0; i < length; i++) {
        char *name = readString(self);
        
        spIkConstraintData *data = spIkConstraintData_create(name);
        int boneCount = readVarint(self, true);
        data->bones = MALLOC(spBoneData *, boneCount);
        data->bonesCount = 0;
        for (int ii = 0; ii < boneCount; ii++) {
            data->bones[data->bonesCount++] = skeletonData->bones[readVarint(self, true)];
        }
        data->target = skeletonData->bones[readVarint(self, true)];
        data->mix = readFloat(self);
        data->bendDirection = readByte(self);
        skeletonData->ikConstraints[skeletonData->ikConstraintsCount++] = data;
    }
    
    // transform constraints
    length = readVarint(self, true);
    skeletonData->transformConstraints = MALLOC(spTransformConstraintData *, length);
    skeletonData->transformConstraintsCount = 0;
    for (int i = 0; i < length; i++) {
        int boneCount;
        char *name = readString(self);
        
        spTransformConstraintData *data = spTransformConstraintData_create(name);
        boneCount = readVarint(self, true);
        CONST_CAST(spBoneData**, data->bones) = MALLOC(spBoneData*, boneCount);
        data->bonesCount = 0;
        for (int ii = 0; ii < boneCount; ii++) {
            data->bones[data->bonesCount++] = skeletonData->bones[readVarint(self, true)];
        }
        data->target = skeletonData->bones[readVarint(self, true)];
        data->offsetRotation = readFloat(self);
        data->offsetX = readFloat(self) * scale;
        data->offsetY = readFloat(self) * scale;
        data->offsetScaleX = readFloat(self);
        data->offsetScaleY = readFloat(self);
        data->offsetShearY = readFloat(self);
        data->rotateMix = readFloat(self);
        data->translateMix = readFloat(self);
        data->scaleMix = readFloat(self);
        data->shearMix = readFloat(self);
        skeletonData->transformConstraints[skeletonData->transformConstraintsCount++] = data;
    };
    
    // path constraints
    length = readVarint(self, true);
    skeletonData->pathConstraints = MALLOC(spPathConstraintData *, length);
    skeletonData->pathConstraintsCount = 0;
    for (int i = 0; i < length; i++) {
        int boneCount;
        char *name = readString(self);
        
        spPathConstraintData *data = spPathConstraintData_create(name);
        boneCount = readVarint(self, true);
        CONST_CAST(spBoneData**, data->bones) = MALLOC(spBoneData*, boneCount);
        data->bonesCount = 0;
        for (int ii = 0; ii < boneCount; ii++) {
            data->bones[data->bonesCount++] = skeletonData->bones[readVarint(self, true)];
        }
        data->target = skeletonData->slots[readVarint(self, true)];
        data->positionMode = (spPositionMode)readVarint(self, true);
        data->spacingMode = (spSpacingMode)readVarint(self, true);
        data->rotateMode = (spRotateMode)readVarint(self, true);
        data->offsetRotation = readFloat(self);
        data->position = readFloat(self);
        if (data->positionMode == SP_POSITION_MODE_FIXED) {
            data->position *= scale;
        }
        data->spacing = readFloat(self);
        if (data->spacingMode == SP_SPACING_MODE_LENGTH || data->spacingMode == SP_POSITION_MODE_FIXED) {
            data->spacing *= scale;
        }
        data->rotateMix = readFloat(self);
        data->translateMix = readFloat(self);
        skeletonData->pathConstraints[skeletonData->pathConstraintsCount++] = data;
    }
    
    // default skin
    skeletonData->defaultSkin = readSkin(self, "default");
    
    // skins
    length = readVarint(self, true);
    skeletonData->skins = MALLOC(spSkin *, skeletonData->defaultSkin == NULL ? length : (length + 1));
    skeletonData->skinsCount = 0;
    if (skeletonData->defaultSkin) {
        skeletonData->skins[skeletonData->skinsCount++] = skeletonData->defaultSkin;
    }
    for (int i = 0; i < length; i++) {
        char *name = readString(self);
        
        skeletonData->skins[skeletonData->skinsCount++] = readSkin(self, name);
    }
    
    // Linked meshes
    for (int i = 0; i < self->linkedMeshCount; i++) {
        spAttachment* parent;
        _spLinkedMesh* linkedMesh = self->linkedMeshes + i;
        spSkin* skin = !linkedMesh->skin ? skeletonData->defaultSkin : spSkeletonData_findSkin(skeletonData, linkedMesh->skin);
        if (!skin) {
            printf("skin not found: %s\n", linkedMesh->skin);
            return;
        }
        parent = spSkin_getAttachment(skin, linkedMesh->slotIndex, linkedMesh->parent);
        if (!parent) {
            printf("Parent mesh not found: %s\n", linkedMesh->parent);
            return;
        }
        spMeshAttachment_setParentMesh(linkedMesh->mesh, SUB_CAST(spMeshAttachment, parent));
        spMeshAttachment_updateUVs(linkedMesh->mesh);
        spAttachmentLoader_configureAttachment(self->attachmentLoader, SUPER(SUPER(linkedMesh->mesh)));
    }

    // events
    length = readVarint(self, true);
    skeletonData->events = MALLOC(spEventData *, length);
    skeletonData->eventsCount = 0;
    for (int i = 0; i < length; i++) {
        char *name = readString(self);
        
        spEventData *data = spEventData_create(name);
        data->intValue = readVarint(self, false);
        data->floatValue = readFloat(self);
        data->stringValue = copyString(readString(self));
        skeletonData->events[skeletonData->eventsCount++] = data;
    }
    
    // animations
    length = readVarint(self, true);
    skeletonData->animations = MALLOC(spAnimation *, length);
    skeletonData->animationsCount = 0;
    for (int i = 0; i < length; i++) {
        char *name = readString(self);
        
        spAnimation *data = readAnimation(self, name);
        skeletonData->animations[skeletonData->animationsCount++] = data;
    }
}

spSkeletonData *spSkeletonBinary_readSkeletonData(const char *skeketonPath, spAttachmentLoader *attachmentLoader, float scale)
{
    spSkeletonData *skeketon;
    spSkeletonBinary *self;
    self = (spSkeletonBinary *)malloc(sizeof(spSkeletonBinary));
    self->scale = scale;
    self->attachmentLoader = attachmentLoader;
    
    self->data = (_spStringBuffer *)malloc(sizeof(_spStringBuffer));
    self->data->next = NULL;
    self->data->position = 0;
    self->data->content = _spUtil_readFile(skeketonPath, &self->data->capacity);
    
    self->buffer = NULL;
    
    self->linkedMeshCount = 0;
    self->linkedMeshCapacity = 0;
    self->linkedMeshes = NULL;
    
    self->skeletonData = spSkeletonData_create();
    readSkeleton(self);
    skeketon = self->skeletonData;
    self->skeletonData = NULL;
    
    while(self->buffer) {
        _spStringBuffer *next = self->buffer->next;
        free(self->buffer->content);
        free(self->buffer);
        self->buffer = next;
    }
    
    free(self->data->content);
    free(self->data);
    free(self->linkedMeshes);
    free(self);
    
    return skeketon;
}