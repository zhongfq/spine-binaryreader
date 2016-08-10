--
-- $id: converter.lua zhongfengqu $
--

local option
local data
local binarywriter

local boneame2idx = {}
local slotname2idx = {}
local skinname2idx = {}
local eventname2idx = {}
local ikname2idx = {}
local transformname2idx = {}
local pathname2idx = {}

local function optionalBool(value, default)
    if value == nil then
        return default
    else
        return value
    end
end

local function writeBool(value)
    if value == 0 or value == nil or value == false then
        value = false
    else
        value = true
    end
    binarywriter:bool(value)
end

local function writeByte(value)
    assert(type(value) == "number")
    binarywriter:byte(value)
end

local function writeShort(value)
    assert(type(value) == "number")
    binarywriter:short(value)
end

local function writeRawShorts(shorts)
    for _, value in ipairs(shorts) do
        writeShort(value)
    end
end

local function writeInt(value, _)
    assert(type(value) == "number")
    assert(_ == nil)
    binarywriter:int(value)
end

local function writeVarint(value, optimizePositive)
    assert(type(value) == "number")
    assert(optimizePositive ~= nil)
    binarywriter:varint(value, optimizePositive)
end

local function writeFloat(value)
    assert(type(value) == "number")
    binarywriter:float(value)
end

local function writeString(value)
    binarywriter:string(value)
end

local function writeColor(color)
    writeInt(tonumber(string.format("0x%s", color or "ffffffff")))
end

local function writeRawFloats(floats)
    for _, value in ipairs(floats) do
        writeFloat(value)
    end
end

local function writeFloats(floats)
    floats = floats or {}
    writeVarint(#floats, true)
    writeRawFloats(floats)
end

local function writeShorts(shorts)
    shorts = shorts or {}
    writeVarint(#shorts, true)
    writeRawShorts(shorts)
end

local function getSortedNames(t)
    local names = {}
    for name, _ in pairs(t or {}) do
        names[#names + 1] = name
    end
    table.sort(names, function(a, b) return a < b end)
    return names
end


-------------------------------------------------------------------------------
-- name index
-------------------------------------------------------------------------------
local function initNameIndex()
    for i, value in ipairs(data.bones or {}) do
        boneame2idx[value.name] = i - 1
    end

    for i, value in ipairs(data.ik or {}) do
        ikname2idx[value.name] = i - 1
    end

    for i, value in ipairs(data.slots or {}) do
        slotname2idx[value.name] = i - 1
    end

    local idx = 0
    if data.skins.default and next(data.skins.default) then
        skinname2idx["default"] = idx;
        idx = idx + 1
    end
    for i, value in ipairs(getSortedNames(data.skins)) do
        if next(data.skins[value]) and value ~= "default" then
            skinname2idx[value] = idx
            idx = idx + 1
        end
    end

    for i, value in ipairs(getSortedNames(data.events)) do
        eventname2idx[value] = i - 1
    end

    for i, value in ipairs(data.path or {}) do 
        pathname2idx[value.name] = i - 1
    end

    for i, value in ipairs(data.transform or {}) do
        transformname2idx[value.name] = i - 1
    end
end

-------------------------------------------------------------------------------
-- trim
-------------------------------------------------------------------------------
local function trimSlotTimelineAttachment(name, slot, timeline)
    if not timeline then return end
    local default = data.slots[slotname2idx[name] + 1].attachment
    local last
    local count = 1
    while count <= #timeline do
        local curr = timeline[count]
        if last and last.name == curr.name then
            table.remove(timeline, count)
        else
            last = curr
            count = count + 1
        end
    end

    if #timeline == 1 and timeline[1].name == default or #timeline == 0 then
        slot.attachment = nil
    end
end

local function trimSlotTimelineColor(name, slot, timeline)
    if not timeline then return end
    local default = string.lower(data.slots[slotname2idx[name] + 1].color or "ffffffff")
    local count = 1
    while count <= #timeline do
        local curr = timeline[count]
        if last and last.color == curr.color then
            table.remove(timeline, count)
        else
            last = curr
            count = count + 1
        end
    end

    if #timeline == 1 and timeline[1].color == default or #timeline == 0 then
        slot.color = nil
    end
end

local function trimSlotTimeline(name, slot, slots)
    trimSlotTimelineAttachment(name, slot, slot.attachment)
    trimSlotTimelineColor(name, slot, slot.color)
    if not next(slot) then
        slots[name] = nil
    end
end

local function isTraScaEqual(last, curr, theend)
    if not last then return false end
    if theend then
        return last.x == curr.x and last.y == curr.y
    else
        return last.x == curr.x and last.y == curr.y and last.curve == curr.curve
    end
end

local function isRoateEqual(last, curr, theend)
    if not last then return false end
    if theend then
        return last.angle == curr.angle
    else
        return last.angle == curr.angle and last.curve == curr.curve
    end
end

local function trimBoneTimeline(name, timeline)
    if #timeline < 3 then return end

    local same = 0
    local i = 1
    local last = nil
    while i <= #timeline do
        local curr = timeline[i]
        local equal
        if name == "rotate" then
            equal = isRoateEqual(last, curr, i == #timeline)
        else
            equal = isTraScaEqual(last, curr, i == #timeline)
        end
        if equal then
            same = same + 1
            if same == 3 then
                table.remove(timeline, i - 1)
                same = same - 1
            else
                i = i + 1
            end
        else
            same = 1
            last = curr
            i = i + 1
        end
    end
end

local function trimSlotTimelines()
    if not option.trim then
        return
    end

    local animations = data.animations
    for _, animation in pairs(animations) do
        for name, slot in pairs(animation.slots or {}) do
            trimSlotTimeline(name, slot, animation.slots)
        end
        for _, bone in pairs(animation.bones or {}) do
            for name, timeline in pairs(bone) do
                trimBoneTimeline(name, timeline)
            end
        end
    end
end

-------------------------------------------------------------------------------
-- makeup
-------------------------------------------------------------------------------
local function makeupTimelines()
    if not option.makeup then
        return
    end

    local attachments = {}
    local flips = {}
    local colors = {}
    local hasDrawOrder = false
    for animationname, animation in pairs(data.animations) do
        for slotname, slot in pairs(animation.slots or {}) do
            attachments[slotname] = attachments[slotname] or slot.attachment
            colors[slotname] = colors[slotname] or slot.color
        end
        hasDrawOrder = animation.drawOrder or hasDrawOrder
        for bonename, bone in pairs(animation.bones or {}) do
            if bone.flipX or bone.flipY then
                flips[bonename] = flips[bonename] or {}
                flips[bonename].flipX = flips[bonename].flipX or bone.flipX
                flips[bonename].flipY = flips[bonename].flipY or bone.flipY
            end
        end
    end

    for animationname, animation in pairs(data.animations) do
        animation.slots = animation.slots or {}
        animation.bones = animation.bones or {}
        local slots = animation.slots
        local bones = animation.bones
        for slotname, _ in pairs(attachments) do
            slots[slotname] = slots[slotname] or {}
            if not slots[slotname].attachment or slots[slotname].attachment[1].time > 0 then
                slots[slotname].attachment = slots[slotname].attachment or {}
                table.insert(slots[slotname].attachment, 1, {
                    time = 0,
                    name = data.slots[slotname2idx[slotname] + 1].attachment,
                })
            end
        end
        for slotname, _ in pairs(colors) do
            slots[slotname] = slots[slotname] or {}
            if not slots[slotname].color or slots[slotname].color[1].time > 0 then
                slots[slotname].color = slots[slotname].color or {}
                table.insert(slots[slotname].color, 1, {
                    time = 0,
                    color = data.slots[slotname2idx[slotname] + 1].color or "ffffffff",
                    curve = "stepped",
                })
            end
        end

        for bonename, flip in pairs(flips) do
            if next(flip) then
                bones[bonename] = bones[bonename] or {}
                if flip.flipX then
                    bones[bonename].flipX = bones[bonename].flipX or {}
                    table.insert(bones[bonename].flipX, 1, {
                        time = 0,
                        x = data.bones[boneame2idx[bonename] + 1].flipX,
                    })
                elseif flip.flipY then
                    bones[bonename].flipY = bones[bonename].flipY or {}
                    table.insert(bones[bonename].flipY, 1, {
                        time = 0,
                        x = data.bones[boneame2idx[bonename] + 1].flipY,
                    })
                end
            end
        end

        if hasDrawOrder and not animation.drawOrder then
            animation.drawOrder = {}
            animation.drawOrder[1] = { time = 0 }
        end
    end
end

-------------------------------------------------------------------------------
-- header
-------------------------------------------------------------------------------
local function writeHeader()
    data.skeleton = data.skeleton or {}
    writeString(data.skeleton.hash)
    writeString(data.skeleton.spine) -- version
    writeFloat(data.skeleton.width or 0)
    writeFloat(data.skeleton.height or 0)
    writeBool(false)
end

-------------------------------------------------------------------------------
-- bones
-------------------------------------------------------------------------------
local function writeBones()
    local bones = data.bones
    writeVarint(#bones, true)
    for i, bone in ipairs(bones) do
        writeString(bone.name)
        if i > 1 then
            writeVarint(bone.parent and boneame2idx[bone.parent], true)
        end
        writeFloat(bone.rotation or 0)
        writeFloat(bone.x or 0)
        writeFloat(bone.y or 0)
        writeFloat(bone.scaleX or 1)
        writeFloat(bone.scaleY or 1)
        writeFloat(bone.shearX or 0)
        writeFloat(bone.shearY or 0)
        writeFloat(bone.length or 0)
        writeBool(optionalBool(bone.inheritRotation, 1))
        writeBool(optionalBool(bone.inheritScale, 1))
    end
end

-------------------------------------------------------------------------------
-- slots
-------------------------------------------------------------------------------
local function writeSlots()
    local BlendMode = {
        normal = 0,
        additive = 1,
        multiply = 2,
        screen = 3,
    }
    local slots = data.slots
    writeVarint(#slots, true)
    for _, slot in ipairs(slots) do
        writeString(slot.name)
        writeVarint(boneame2idx[slot.bone], true)
        writeColor(slot.color)
        writeString(slot.attachment)
        writeVarint(BlendMode[slot.blend or "normal"], true)
    end
end

-------------------------------------------------------------------------------
-- iks
-------------------------------------------------------------------------------
local function writeIKs()
    local iks = data.ik or {}
    writeVarint(#iks, true)
    for _, ik in ipairs(iks) do
        writeString(ik.name)
        writeVarint(#ik.bones, true)
        for _, bone in ipairs(ik.bones) do
            writeVarint(boneame2idx[bone], true)
        end
        writeVarint(boneame2idx[ik.target] or 0, true)
        writeFloat(ik.mix or 1)
        writeByte((ik.bendPositive or 1) ~= 0 and 1 or -1)
    end
end

------------------------------------------------------------------------------
-- transform constraints
------------------------------------------------------------------------------
local function writeTransformConstraints()
    local transforms = data.transform or {}
    writeVarint(#transforms, true)
    for _, tf in ipairs(transforms) do
        writeString(tf.name)
        writeVarint(#tf.bones, true)
        for _, bone in ipairs(tf.bones) do
            writeVarint(boneame2idx[bone], true)
        end
        writeVarint(boneame2idx[tf.target] or 0, true)
        writeFloat(tf.rotation or 0)
        writeFloat(tf.x or 0)
        writeFloat(tf.y or 0)
        writeFloat(tf.scaleX or 0)
        writeFloat(tf.scaleY or 0)
        writeFloat(tf.shearY or 0)
        writeFloat(tf.rotateMix or 1)
        writeFloat(tf.translateMix or 1)
        writeFloat(tf.scaleMix or 1)
        writeFloat(tf.shearMix or 1)
    end
end

------------------------------------------------------------------------------
-- path constraints
------------------------------------------------------------------------------
local function writePathConstraints()
    local PositionMode = {fixed = 0, percent = 1}
    local SpaceingMode = {length = 0, fixed = 1, percent = 2}
    local RotateMode = {tangent = 0, chain = 1, chainScale = 2}
    local paths = data.path or {}
    writeVarint(#paths, true)
    for _, path in ipairs(paths) do
        writeString(path.name)
        writeVarint(#path.bones, true)
        for _, bone in ipairs(path.bones) do
            writeVarint(boneame2idx[bone], true)
        end
        writeVarint(slotname2idx[path.target] or 0, true)
        writeVarint(PositionMode[path.positionMode or "percent"], true)
        writeVarint(SpaceingMode[path.spacingMode or "length"], true)
        writeVarint(RotateMode[path.rotateMode or "tangent"], true)
        writeFloat(path.rotation or 0)
        writeFloat(path.position or 0)
        writeFloat(path.spacing or 0)
        writeFloat(path.rotateMix or 1)
        writeFloat(path.translateMix or 1)
    end
end

-------------------------------------------------------------------------------
-- skins
-------------------------------------------------------------------------------
local AttachmentType = {
    region = 0,
    boundingbox = 1,
    mesh = 2,
    linkedmesh = 3,
    path = 4
}

local function calculateVertexCount(vertices, verticesLength)
    if #vertices == verticesLength then
        return verticesLength >> 1
    else
        local count = 0
        local i = 1
        while i <= #vertices do
            local boneCount = math.floor(vertices[i])
            i = i + 1
            count = count + 1
            for b = 1, boneCount do
                i = i + 4
            end
        end
        return count
    end
end

local function writeVertices(vertices, verticesLength)
    if #vertices == verticesLength then
        writeBool(false)
        writeRawFloats(vertices)
    else
        writeBool(true)
        local i = 1
        while i <= #vertices do
            local boneCount = math.floor(vertices[i])
            i = i + 1
            writeVarint(boneCount, true)
            for b = 1, boneCount do
                writeVarint(math.floor(vertices[i]), true)
                writeFloat(vertices[i + 1])
                writeFloat(vertices[i + 2])
                writeFloat(vertices[i + 3])
                i = i + 4
            end
        end
    end
end

local function writeAttachment(attachment)
    attachment.type = attachment.type or "region"

    writeString(attachment.name)
    writeByte(AttachmentType[attachment.type])

    if attachment.type == "region" then
        writeString(attachment.path)
        writeFloat(attachment.rotation or 0)
        writeFloat(attachment.x or 0)
        writeFloat(attachment.y or 0)
        writeFloat(attachment.scaleX or 1)
        writeFloat(attachment.scaleY or 1)
        writeFloat(attachment.width or 32)
        writeFloat(attachment.height or 32)
        writeColor(attachment.color)
    elseif attachment.type == "boundingbox" then
        attachment.vertexCount = attachment.vertexCount or 0
        local vertexCount = calculateVertexCount(attachment.vertices,
            attachment.vertexCount << 1)
        writeVarint(vertexCount, true)
        writeVertices(attachment.vertices, attachment.vertexCount << 1)
    elseif attachment.type == "mesh" then
        local vertexCount = calculateVertexCount(attachment.vertices,
            #attachment.uvs)
        assert(vertexCount == #attachment.uvs >> 1, attachment.name)
        writeString(attachment.path)
        writeColor(attachment.color)
        writeVarint(vertexCount, true)
        writeRawFloats(attachment.uvs)
        writeShorts(attachment.triangles)
        writeVertices(attachment.vertices, #attachment.uvs)
        writeVarint((attachment.hull or 0) >> 1, true)
    elseif attachment.type == "linkedmesh" then
        writeString(attachment.path)
        writeColor(attachment.color)
        writeString(attachment.skin)
        writeString(attachment.parent)
        writeBool(optionalBool(attachment.deform, 1))
    elseif attachment.type == "path" then
        attachment.vertexCount = attachment.vertexCount or 0
        local vertexCount = calculateVertexCount(attachment.vertices,
            attachment.vertexCount << 1)
        assert(vertexCount == attachment.vertexCount, attachment.name)
        assert(vertexCount / 3 == #attachment.lengths, attachment.name)
        writeBool(optionalBool(attachment.closed, 0))
        writeBool(optionalBool(attachment.constantSpeed, 1))
        writeVarint(vertexCount, true)
        writeVertices(attachment.vertices, attachment.vertexCount << 1)
        writeRawFloats(attachment.lengths)
    else
        assert(false, "unknown attachment type: " .. attachment.type)
    end
end

local function writeSkin(skin)
    local slotNames = getSortedNames(skin or {})
    writeVarint(#slotNames, true)
    for i, name in ipairs(slotNames) do
        local attachmentNames = getSortedNames(skin[name])
        writeVarint(slotname2idx[name], true)
        writeVarint(#attachmentNames, true)
        for i, attachmentName in ipairs(attachmentNames) do
            writeString(attachmentName)
            writeAttachment(skin[name][attachmentName])
        end
    end
end

local function writeSkins()
    local skins = data.skins or {}

    -- default skin
    writeSkin(skins.default)

    local default = skins.default
    skins.default = nil

    local names = getSortedNames(skins)
    writeVarint(#names, true)
    for i, name in ipairs(names) do
        if name ~= "default" then
            writeString(name)
            writeSkin(skins[name])
        end
    end

    skins.default = default
end

-------------------------------------------------------------------------------
-- events
-------------------------------------------------------------------------------
local function writeEvents()
    local events = data.events or {}
    local names = getSortedNames(events)
    writeVarint(#names, true)
    for i, name in ipairs(names) do
        writeString(name)
        writeVarint(events[name].int or 0, false)
        writeFloat(events[name].float or 0)
        writeString(events[name].string)
    end
end

-------------------------------------------------------------------------------
-- animations
-------------------------------------------------------------------------------
local TimelineType = {
    rotate      = 0,
    translate   = 1,
    scale       = 2,
    shear       = 3,

    attachment  = 0,
    color       = 1,

    position    = 0,
    spacing     = 1,
    mix         = 2,
}

local CurveType = {
    linear = 0,
    stepped = 1,
    curve = 2,
}

local function writeCurve(curve)
    if curve == "stepped" then
        writeByte(CurveType.stepped)
    elseif type(curve) == "table" then
        writeByte(CurveType.curve)
        assert(#curve == 4)
        for _, v in ipairs(curve) do
            writeFloat(v)
        end
    else
        writeByte(CurveType.linear)
    end
end

local function writeAnimationSlots(slots)
    local names = getSortedNames(slots)
    writeVarint(#names, true)
    for _, slot in ipairs(names) do
        local timelinenames = getSortedNames(slots[slot])
        writeVarint(slotname2idx[slot], true)
        writeVarint(#timelinenames, true)
        for _, name in ipairs(timelinenames) do
            local timeline = slots[slot][name]
            writeByte(TimelineType[name])
            writeVarint(#timeline, true)
            if name == "attachment" then
                for _, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeString(frame.name)
                end
            elseif name == "color" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeColor(frame.color)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            end
        end
    end
end

local function writeAnimationBones(bones)
    local names = getSortedNames(bones)
    writeVarint(#names, true)
    for _, bone in ipairs(names) do
        local timelinenames = getSortedNames(bones[bone])
        writeVarint(boneame2idx[bone], true)
        writeVarint(#timelinenames, true)
        for _, name in ipairs(timelinenames) do
            local timeline = bones[bone][name]
            writeByte(TimelineType[name])
            writeVarint(#timeline, true)
            if name == "rotate" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeFloat(frame.angle or 0)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end  
            elseif name == "translate" or name == "scale" or name == "shear" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeFloat(frame.x or 0)
                    writeFloat(frame.y or 0)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            else 
                print("unknown timeline type: " .. name)
            end
        end
    end
end

local function writeAnimationIKs(iks)
    local names = getSortedNames(iks)
    writeVarint(#names, true)
    for _, name in ipairs(names) do
        local timeline = iks[name]
        writeVarint(ikname2idx[name], true)
        writeVarint(#timeline, true)
        for i, frame in ipairs(timeline) do
            writeFloat(frame.time or 0)
            writeFloat(frame.mix or 1)
            writeByte((frame.bendPositive or 1) ~= 0 and 1 or -1)
            if i < #timeline then
                writeCurve(frame.curve)
            end
        end
    end
end

local function writeAnimationTransformConstraints(transforms)
    local names = getSortedNames(transforms)
    writeVarint(#names, true)
    for _, name in ipairs(names) do
        local timeline = transforms[name]
        writeVarint(transformname2idx[name], true)
        writeVarint(#timeline, true)
        for i, frame in ipairs(timeline) do
            writeFloat(frame.time or 0)
            writeFloat(frame.rotateMix or 1)
            writeFloat(frame.translateMix or 1)
            writeFloat(frame.scaleMix or 1)
            writeFloat(frame.shearMix or 1)
            if i < #timeline then
                writeCurve(frame.curve)
            end
        end
    end
end

local function writeAnimationPathConstraints(paths)
    local names = getSortedNames(paths)
    writeVarint(#names, true)
    for _, slot in ipairs(names) do
        local timelinenames = getSortedNames(paths[slot])
        writeVarint(pathname2idx[slot], true)
        writeVarint(#timelinenames, true)
        for _, name in ipairs(timelinenames) do
            local timeline = paths[slot][name]
            writeByte(TimelineType[name])
            writeVarint(#timeline, true)
            if name == "position" or name == "spacing" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeFloat(frame[name] or 0)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            elseif name == "mix" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time or 0)
                    writeFloat(frame.rotateMix or 1)
                    writeFloat(frame.translateMix or 1)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            end
        end
    end
end

local function writeAnimationDeforms(deforms)
    local skinNames = getSortedNames(deforms)
    writeVarint(#skinNames, true)
    for _, skin in ipairs(skinNames) do
        local slotNames = getSortedNames(deforms[skin])
        writeVarint(skinname2idx[skin], true)
        writeVarint(#slotNames, true)
        for _, slot in ipairs(slotNames) do
            local timelinenames = getSortedNames(deforms[skin][slot])
            writeVarint(slotname2idx[slot], true)
            writeVarint(#timelinenames, true)
            for _, name in ipairs(timelinenames) do
                local timeline = deforms[skin][slot][name]
                writeString(name)
                writeVarint(#timeline, true)
                for i, frame in ipairs(timeline) do
                    frame.vertices = frame.vertices or {}
                    writeFloat(frame.time or 0)
                    writeVarint(#frame.vertices, true)
                    writeVarint(frame.offset or 0, true)
                    writeRawFloats(frame.vertices)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            end
        end
    end
end

local function writeAnimationDraworder(draworder)
    writeVarint(#draworder, true)
    for _, frame in ipairs(draworder) do
        local offsets = frame.offsets or {}
        writeFloat(frame.time or 0)
        writeVarint(#offsets, true)
        for _, offset in ipairs(offsets) do
            writeVarint(slotname2idx[offset.slot], true)
            writeVarint(offset.offset or 0, true)
        end
    end
end

local function writeAnimationEvents(events)
    writeVarint(#events, true)
    for _, frame in ipairs(events) do
        writeFloat(frame.time or 0)
        writeVarint(eventname2idx[frame.name], true)
        writeVarint(frame.int or 0, false)
        writeFloat(frame.float or 0)
        writeBool(frame.string ~= nil)
        if frame.string ~= nil then
            writeString(frame.string)
        end
    end
end

local function writeAnimation(animation)
    writeAnimationSlots(animation.slots or {})
    writeAnimationBones(animation.bones or {})
    writeAnimationIKs(animation.ik or {})
    writeAnimationTransformConstraints(animation.transform or {})
    writeAnimationPathConstraints(animation.paths or {})
    writeAnimationDeforms(animation.deform or {})
    writeAnimationDraworder(animation.drawOrder or {})
    writeAnimationEvents(animation.events or {})
end

local function writeAnimations()
    local animations = data.animations
    local names = getSortedNames(animations)
    writeVarint(#names, true)
    for _, name in ipairs(names) do
        writeString(name)
        writeAnimation(animations[name])
    end
end

local function readData(jsonfile)
    local file = io.open(jsonfile, "r");
    local data = file:read("*a")
    file:close()
    return cjson.decode(data)
end

function main(jsonfile, skelfile, cmdoption)
    option = cmdoption
    data = readData(jsonfile)
    binarywriter = spinewriter.new(skelfile)
    initNameIndex()
    trimSlotTimelines()
    makeupTimelines()
    writeHeader()
    writeBones()
    writeSlots()
    writeIKs()
    writeTransformConstraints()
    writePathConstraints()
    writeSkins()
    writeEvents()
    writeAnimations()
end