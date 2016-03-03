//
// $id: SkeletonBinary.c 2014-08-06 zhongfengqu $
//

#include "SkeletonBinary.h"
#include "spine/extension.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef enum {
    SP_CURVE_LINEAR,
    SP_CURVE_STEPPED,
    SP_CURVE_BEZIER,
} spCurveType;

typedef struct {
    spSkeletonJson *json;
    float scale;
    char *rawdata;
    char *reader;
    char **strs;
} spSkeletonBinary;

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))
#define READ() (((int)*self->reader++) & 0xFF)

#define inline __inline

static inline int readBoolean(spSkeletonBinary *self)
{
    int ch = READ();
    return ch != 0;
}

static inline char readChar(spSkeletonBinary *self)
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

static inline float readFloat(spSkeletonBinary *self)
{
    union {
        float f;
        int i;
    } u;

    u.i = readInt(self);
    return u.f;
}

static inline const char *readString(spSkeletonBinary *self)
{
    short index = readShort(self);
    if (index < 0) return NULL;
    return self->strs[index];
}

static inline float *readFloats(spSkeletonBinary *self, float scale, size_t *length)
{
    float *arr;
    int i;
    int n = readInt(self);
    *length = n;
    arr = MALLOC(float, n);
    for (i = 0; i < n; i++)
    {
        arr[i] = readFloat(self) * scale;
    }

    return arr;
}

static inline int *readShorts(spSkeletonBinary *self, size_t *length)
{
    int *arr;
    int i;
    int n = readInt(self);
    *length = n;
    arr = MALLOC(int, n);
    for (i = 0; i < n; i++)
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

static spAttachment *readAttachment(spSkeletonBinary *self, spSkin *Skin, const char *attachmentName)
{
    float scale = self->scale;
    const char *name = readString(self);
    if (name == NULL) name = attachmentName;

    switch (readChar(self))
    {
        case SP_ATTACHMENT_REGION:
        {
            spAttachment *attachment;
            spRegionAttachment *region;
            const char *path = readString(self);
            if (path == NULL) path = name;

            attachment = spAttachmentLoader_newAttachment(self->json->attachmentLoader, 
                Skin, SP_ATTACHMENT_REGION, attachmentName, path);
            region = SUB_CAST(spRegionAttachment, attachment);
            if (path) MALLOC_STR(region->path, path);

            region->x = readFloat(self) * scale;
            region->y = readFloat(self) * scale;
            region->scaleX = readFloat(self);
            region->scaleY = readFloat(self);
            region->rotation = readFloat(self);
            region->width = readFloat(self) * scale;
            region->height = readFloat(self) * scale;
            readColor(self, &region->r, &region->g, &region->b, &region->a);

            spRegionAttachment_updateOffset(region);

            return SUPER_CAST(spAttachment, region);
        }
        case SP_ATTACHMENT_BOUNDING_BOX: 
        {
            size_t length;

            spAttachment *attachment = spAttachmentLoader_newAttachment(self->json->attachmentLoader,
                Skin, SP_ATTACHMENT_BOUNDING_BOX, attachmentName, NULL);
            spBoundingBoxAttachment *box = SUB_CAST(spBoundingBoxAttachment, attachment);

            box->vertices = readFloats(self, scale, &length);
            box->verticesCount = (int)length;

            return SUPER_CAST(spAttachment, box);
        }
        case SP_ATTACHMENT_MESH: 
        {
            size_t length;
            spAttachment *attachment;
            spMeshAttachment *mesh;
            const char *path = readString(self);
            if (path == NULL) path = name;

            attachment = spAttachmentLoader_newAttachment(self->json->attachmentLoader, 
                Skin, SP_ATTACHMENT_MESH, attachmentName, path);
            mesh = SUB_CAST(spMeshAttachment, attachment);
            if (path) MALLOC_STR(mesh->path, path);

            mesh->regionUVs = readFloats(self, 1, &length);
            mesh->triangles = readShorts(self, &length);
            mesh->trianglesCount = (int)length;
            mesh->vertices = readFloats(self, scale, &length);
            mesh->verticesCount = (int)length;
            readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
            mesh->hullLength = readInt(self) * 2;

            spMeshAttachment_updateUVs(mesh);

            return SUPER_CAST(spAttachment, mesh);
        }
        case SP_ATTACHMENT_SKINNED_MESH: 
        {
            int verticesCount, b, w, nn, i;
            size_t length;
            spAttachment *attachment;
            spSkinnedMeshAttachment *mesh;
            const char *path = readString(self);
            if (path == NULL) path = name;

            attachment = spAttachmentLoader_newAttachment(self->json->attachmentLoader, 
                Skin, SP_ATTACHMENT_SKINNED_MESH, attachmentName, path);
            mesh = SUB_CAST(spSkinnedMeshAttachment, attachment);
            if (path) MALLOC_STR(mesh->path, path);
            
            mesh->regionUVs = readFloats(self, 1, &length);
            mesh->uvsCount = (int)length;
            mesh->triangles = readShorts(self, &length);
            mesh->trianglesCount = (int)length;

            verticesCount = readInt(self);

            mesh->weightsCount = mesh->uvsCount * 3 * 3;
            mesh->weights = CALLOC(float, mesh->weightsCount);
            mesh->bonesCount = mesh->uvsCount * 3;
            mesh->bones = CALLOC(int, mesh->bonesCount);
            for (i = 0, b = 0, w = 0; i < verticesCount; i++)
            {
                int boneCount = (int)readFloat(self);
                mesh->bones[b++] = boneCount;
                for (nn = i + boneCount * 4; i < nn; i += 4, ++b, w += 3)
                {
                    mesh->bones[b] = (int)readFloat(self);
                    mesh->weights[w] = readFloat(self) * scale;
                    mesh->weights[w + 1] = readFloat(self) * scale;
                    mesh->weights[w + 2] = readFloat(self);
                }
            }
            readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
            mesh->hullLength = readInt(self) * 2;

            spSkinnedMeshAttachment_updateUVs(mesh);

            return SUPER_CAST(spAttachment, mesh);
        }
    }

    return NULL;
}

static spSkin *readSkin(spSkeletonBinary *self, const char *skinName)
{
    spSkin *skin;
    int i;

    int slotCount = readInt(self);

    skin = spSkin_create(skinName);
    for (i = 0; i < slotCount; i++)
    {
        int ii, nn;
        int slotIndex = readInt(self);
        for (ii = 0, nn = readInt(self); ii < nn; ii++)
        {
            const char *name = readString(self);
            spSkin_addAttachment(skin, slotIndex, name, readAttachment(self, skin, name));
        }
    }

    return skin;
}

static void readCurve(spSkeletonBinary *self, spCurveTimeline *timeline, int frameIndex)
{
    spCurveType type = (spCurveType)readChar(self);
    if (type == SP_CURVE_STEPPED)
    {
        spCurveTimeline_setStepped(timeline, frameIndex);
    }
    else if (type == SP_CURVE_BEZIER)
    {
        float v1 = readFloat(self);
        float v2 = readFloat(self);
        float v3 = readFloat(self);
        float v4 = readFloat(self);
        spCurveTimeline_setCurve(timeline, frameIndex, v1, v2, v3, v4);
    }
}

static void readAnimation(spSkeletonBinary *self, spSkeletonData *skeletonData, const char *name)
{
    int i, ii, n, nn;
    float scale = self->scale;
    spAnimation *animation = spAnimation_create(name, readInt(self));
    animation->timelinesCount = 0;

    // Slot timelines
    n = readInt(self);
    for (i = 0; i < n; i++)
    {
        int slotIndex = readInt(self);
        nn = readInt(self);
        for (ii = 0; ii < nn; ii++)
        {
            int frameIndex;
            int timelineType = readChar(self);
            int framesCount = readInt(self);
            switch (timelineType)
            {
                case SP_TIMELINE_COLOR:
                {
                    spColorTimeline *timeline = spColorTimeline_create(framesCount);
                    timeline->slotIndex = slotIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        float r = READ() / (float)255;
                        float g = READ() / (float)255;
                        float b = READ() / (float)255;
                        float a = READ() / (float)255;
                        spColorTimeline_setFrame(timeline, frameIndex, time, r, g, b, a);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount * 5 - 5]);
                    break;
                }
                case SP_TIMELINE_ATTACHMENT:
                {
                    spAttachmentTimeline *timeline = spAttachmentTimeline_create(framesCount);
                    timeline->slotIndex = slotIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        spAttachmentTimeline_setFrame(timeline, frameIndex, time, readString(self));
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount - 1]);
                    break;
                }
            }
        }
    }

    // Bone timelines.
    n = readInt(self);
    for (i = 0; i < n; i++)
    {
        int boneIndex = readInt(self);
        nn = readInt(self);
        for (ii = 0; ii < nn; ii++)
        {
            int frameIndex;
            spTimelineType timelineType = (spTimelineType)readChar(self);
            int framesCount = readInt(self);
            switch (timelineType)
            {
                case SP_TIMELINE_ROTATE:
                {
                    spRotateTimeline *timeline = spRotateTimeline_create(framesCount);
                    timeline->boneIndex = boneIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        float angle = readFloat(self);
                        spRotateTimeline_setFrame(timeline, frameIndex, time, angle);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount * 2 - 2]);
                    break;
                }
                case SP_TIMELINE_TRANSLATE:
                case SP_TIMELINE_SCALE:
                {
                    spTranslateTimeline *timeline;
                    float timelineScale = 1;
                    if (timelineType == SP_TIMELINE_SCALE)
                    {
                        timeline = spScaleTimeline_create(framesCount);
                    }
                    else
                    {
                        timeline = spTranslateTimeline_create(framesCount);
                        timelineScale = scale;
                    }
                    timeline->boneIndex = boneIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        float x = readFloat(self) * timelineScale;
                        float y = readFloat(self) * timelineScale;
                        spTranslateTimeline_setFrame(timeline, frameIndex, time, x, y);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount * 3 - 3]);
                    break;
                }
                case SP_TIMELINE_FLIPX:
                case SP_TIMELINE_FLIPY:
                {
                    spFlipTimeline *timeline = spFlipTimeline_create(framesCount, timelineType == SP_TIMELINE_FLIPX);
                    timeline->boneIndex = boneIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        spFlipTimeline_setFrame(timeline, frameIndex, time, readBoolean(self));
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount * 2 - 2]);
                    break;
                }
                default:
                    break;
            }
        }
    }
    
    //ik timelines
    n = readInt(self);
    for (i = 0; i < n; i++)
    {
        int frameIndex;
        int index = readInt(self);
        int framesCount = readInt(self);
        spIkConstraintTimeline *timeline = spIkConstraintTimeline_create(framesCount);
        timeline->ikConstraintIndex = index;
        for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
        {
            float time = readFloat(self);
            float mix = readFloat(self);
            int bendPositive = readBoolean(self);
            spIkConstraintTimeline_setFrame(timeline, frameIndex, time, mix, bendPositive);
            if (frameIndex < framesCount - 1)
                readCurve(self, SUPER(timeline), frameIndex);
        }
        animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
        animation->duration = MAX(animation->duration, timeline->frames[framesCount * 3 - 3]);
    }

    // FFD timelines
    n = readInt(self);
    for (i = 0; i < n; i++)
    {
        spSkin *skin = skeletonData->skins[readInt(self)];
        nn = readInt(self);
        for (ii = 0; ii < nn; ii++)
        {
            int slotIndex = readInt(self);
            int iii, nnn;
            nnn = readInt(self);
            for (iii = 0; iii < nnn; iii++)
            {
                int frameIndex = 0;
                int framesCount = 0;
                int verticesCount = 0;         
                float *tempVertices;
                spFFDTimeline *timeline;

                spAttachment *attachment = spSkin_getAttachment(skin, slotIndex, readString(self));
                if (attachment->type == SP_ATTACHMENT_MESH)
                    verticesCount = SUB_CAST(spMeshAttachment, attachment)->verticesCount;
                else if (attachment->type == SP_ATTACHMENT_SKINNED_MESH)
                    verticesCount = SUB_CAST(spSkinnedMeshAttachment, attachment)->weightsCount / 3 * 2;

                framesCount = readInt(self);
                timeline = spFFDTimeline_create(framesCount, verticesCount);
                timeline->slotIndex = slotIndex;
                timeline->attachment = attachment;

                tempVertices = MALLOC(float, verticesCount);
                for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                {
                    float *frameVertices;
                    float time = readFloat(self);
                    int start = readInt(self);
                    int end = readInt(self);

                    if (end == 0)
                    {
                        if (attachment->type == SP_ATTACHMENT_MESH)
                            frameVertices = SUB_CAST(spMeshAttachment, attachment)->vertices;
                        else {
                            frameVertices = tempVertices;
                            memset(frameVertices, 0, sizeof(float) * verticesCount);
                        }
                    }
                    else
                    {
                        int v;
                        frameVertices = tempVertices;
                        end += start;
                        if (scale == 1)
                        {
                            for (v = start; v < end; v++)
                                frameVertices[v] = readFloat(self);
                        }
                        else
                        {
                            for (v = start; v < end; v++)
                                frameVertices[v] = readFloat(self) * scale;
                        }
                        memset(frameVertices + v, 0, sizeof(float) * (verticesCount - v));
                        if (attachment->type == SP_ATTACHMENT_MESH) 
                        {
                            float *meshVertices = SUB_CAST(spMeshAttachment, attachment)->vertices;
                            for (v = 0; v < verticesCount; ++v)
                                frameVertices[v] += meshVertices[v];
                        }
                    }

                    spFFDTimeline_setFrame(timeline, frameIndex, time, frameVertices);
                    if (frameIndex < framesCount - 1)
                        readCurve(self, SUPER(timeline), frameIndex);
                }
                FREE(tempVertices);
                animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                animation->duration = MAX(animation->duration, timeline->frames[framesCount - 1]);
            }
        }
    }

    // Draw order timeline
    n = readInt(self);
    if (n > 0)
    {
        int* drawOrder = 0;
        spDrawOrderTimeline *timeline = spDrawOrderTimeline_create(n, skeletonData->slotsCount);
        int slotCount = skeletonData->slotsCount;
        int frameIndex;
        for (frameIndex = 0; frameIndex < n; frameIndex++)
        {
            int originalIndex = 0, unchangedIndex = 0;
            int offsetCount = readInt(self);
            int *unchanged = MALLOC(int, skeletonData->slotsCount - offsetCount);
            drawOrder = MALLOC(int, skeletonData->slotsCount);
            for (ii = slotCount - 1; ii >= 0; ii--)
                drawOrder[ii] = -1;
            for (ii = 0; ii < offsetCount; ii++)
            {
                int slotIndex = readInt(self);
                while (originalIndex != slotIndex)
                    unchanged[unchangedIndex++] = originalIndex++;
                drawOrder[originalIndex + readInt(self)] = originalIndex;
                originalIndex++;
            }
            while (originalIndex < slotCount)
                unchanged[unchangedIndex++] = originalIndex++;
            for (ii = slotCount - 1; ii >= 0; ii--)
                if (drawOrder[ii] == -1) drawOrder[ii] = unchanged[--unchangedIndex];
            FREE(unchanged);
            spDrawOrderTimeline_setFrame(timeline, frameIndex, readFloat(self), drawOrder);
            FREE(drawOrder);
        }
        animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
        animation->duration = MAX(animation->duration, timeline->frames[n - 1]);
    }

    // Event timeline.
    n = readInt(self);
    if (n > 0)
    {
        spEventTimeline *timeline = spEventTimeline_create(n);
        int frameIndex;
        for (frameIndex = 0; frameIndex < n; frameIndex++)
        {
            spEvent *event;
            const char *stringValue;
            spEventData *eventData = skeletonData->events[readInt(self)];
            float time = readFloat(self);
            event = spEvent_create(eventData);
            event->intValue = readInt(self);
            event->floatValue = readFloat(self);
            stringValue = readString(self);
//            stringValue = readBoolean(self) ? readString(self) : eventData->stringValue;
            if (stringValue) MALLOC_STR(event->stringValue, stringValue);
            spEventTimeline_setFrame(timeline, frameIndex, time, event);
        }
        animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
        animation->duration = MAX(animation->duration, timeline->frames[n - 1]);
    }

    skeletonData->animations[skeletonData->animationsCount++] = animation;
}

static spSkeletonData *readSkeleton(spSkeletonBinary *self)
{
    int size, i;
    const char* buff;
    spSkeletonData *skeletonData;
    float scale = self->scale;

    FREE(self->json->error);
    CONST_CAST(char *, self->json->error) = 0;

    skeletonData = spSkeletonData_create();

    // Header
    if ((buff = readString(self)) != NULL) 
        MALLOC_STR(skeletonData->hash, buff);
    if ((buff = readString(self)) != NULL) 
        MALLOC_STR(skeletonData->version, buff);
    skeletonData->width = readFloat(self);
    skeletonData->height = readFloat(self);

    // Bones
    size = readInt(self);
    skeletonData->bones = MALLOC(spBoneData *, size);
    for (i = 0; i < size; i++)
    {
        spBoneData *parent = NULL;
        spBoneData *boneData;
        int parentIndex;

        const char *name = readString(self);
        parentIndex = readInt(self);
        if (parentIndex > -1) parent = skeletonData->bones[parentIndex];
        boneData = spBoneData_create(name, parent);
        boneData->x = readFloat(self) * scale;
        boneData->y = readFloat(self) * scale;
        boneData->scaleX = readFloat(self);
        boneData->scaleY = readFloat(self);
        boneData->rotation = readFloat(self);
        boneData->length = readFloat(self) * scale;
        boneData->flipX = readBoolean(self);
        boneData->flipY = readBoolean(self);
        boneData->inheritScale = readBoolean(self);
        boneData->inheritRotation = readBoolean(self);
        skeletonData->bones[i] = boneData;
        ++skeletonData->bonesCount;
    }
    
    //ik
    size = readInt(self);
    skeletonData->ikConstraints = MALLOC(spIkConstraintData *, size);
    for (i = 0; i < size; i++)
    {
        int n;
        spIkConstraintData *ik = spIkConstraintData_create(readString(self));
        int boneCount = readInt(self);
        ik->bones = MALLOC(spBoneData *, boneCount);
        for (n = 0; n < boneCount; n++)
        {
            ik->bones[ik->bonesCount++] = skeletonData->bones[readInt(self)];
        }
        ik->target = skeletonData->bones[readInt(self)];
        ik->mix = readFloat(self);
        ik->bendDirection = readBoolean(self);
    }

    // Slots
    size = readInt(self);
    if (size > 0)
    {
        skeletonData->slots = MALLOC(spSlotData *, size);
        for (i = 0; i < size; i++) {
            const char *attachment;
            spBoneData *boneData;
            spSlotData *slotData;
           
            const char *name = readString(self);
            boneData = skeletonData->bones[readInt(self)];
            slotData = spSlotData_create(name, boneData);
            readColor(self, &slotData->r, &slotData->g, &slotData->b, &slotData->a);
            attachment = readString(self);
            if (attachment) spSlotData_setAttachmentName(slotData, attachment);

            slotData->additiveBlending = readBoolean(self);

            skeletonData->slots[i] = slotData;
            ++skeletonData->slotsCount;
        }
    }

    // user skin
    size = readInt(self);
    // Skins
    if (size > 0)
    {
        skeletonData->skins = MALLOC(spSkin *, size);
        for (i = 0; i < size; i++)
        {
            const char *name = readString(self);
            spSkin *skin = readSkin(self, name);
            skeletonData->skins[skeletonData->skinsCount] = skin;
            ++skeletonData->skinsCount;
            if (strcmp("default", name) == 0)
                skeletonData->defaultSkin = skin;
        }
    }

    // Events
    size = readInt(self);
    if (size > 0)
    {
        const char *stringValue;
        skeletonData->events = MALLOC(spEventData *, size);
        for (i = 0; i < size; i++)
        {
            spEventData *eventData = spEventData_create(readString(self));
            eventData->intValue = readInt(self);
            eventData->floatValue = readFloat(self);
            stringValue = readString(self);
            if (stringValue) MALLOC_STR(eventData->stringValue, stringValue);
            skeletonData->events[skeletonData->eventsCount++] = eventData;
        }
    }

    // Animations
    size = readInt(self);
    if (size > 0)
    {
        skeletonData->animations = MALLOC(spAnimation *, size);
        for (i = 0; i < size; i++)
        {
            const char *name = readString(self);
            readAnimation(self, skeletonData, name);
        }
    }

    return skeletonData;
}

static void initStrs(spSkeletonBinary *self)
{
    int size, i;
    int skip = readInt(self);
    self->reader = self->rawdata + skip;
    size = readInt(self);
    self->strs = (char **)malloc(sizeof(char *) * size);
    for (i = 0; i < size; i++)
    {
        int len = readShort(self);
        self->strs[i] = self->reader;
        self->strs[i][len] = '\0';
        self->reader += len + 1;
    }
    
    self->reader = self->rawdata + 4;
}

spSkeletonData *spSkeletonBinary_readSkeletonData(const char *skeketonPath, spAtlas *atlas, float scale)
{
    int length;
    spSkeletonData *skeketon;
    spSkeletonBinary *self;
    self = (spSkeletonBinary *)malloc(sizeof(spSkeletonBinary));
    self->scale = scale;
    self->json = spSkeletonJson_create(atlas);
    self->json->scale = scale;
    self->rawdata = _spUtil_readFile(skeketonPath, &length);
    self->reader = self->rawdata;
    
    initStrs(self);

    skeketon = readSkeleton(self);
    spSkeletonJson_dispose(self->json);
    free(self->strs);
    free(self->rawdata);
    free(self);
    return skeketon;
}