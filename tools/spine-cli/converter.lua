--
-- $id: converter.lua 2014-09-05 zhongfengqu $
--

local data
local file

local idx2str = {}
local str2idx = {}
local bonename2idx = {}
local slotname2idx = {}
local skinname2idx = {}
local eventname2idx = {}
local ikname2idx = {}

local timelinetypes = {
    scale       = 0,
    rotate      = 1,
    translate   = 2,
    attachment  = 3,
    color       = 4,
    event       = 5,
    draworder   = 6,
    ffd         = 7,
    ik          = 8,
    flipX       = 9,
    flipY       = 10,
}

function table.tostring(obj, deep)  
    local serialize 
    function serialize(obj, deep, filter)
        deep = deep or 1
        local lua = ""  
        local t = type(obj)  

        if t == "number" then  
            lua = lua .. obj  
        elseif t == "boolean" then  
            lua = lua .. tostring(obj)  
        elseif t == "string" then  
            lua = lua .. string.format("%q", obj)  
        elseif t == "table" then
            if filter[obj] then return tostring(obj) end
            filter[obj] = true
            lua = lua .. "{\n"  
            for k, v in pairs(obj) do
                if (type(k) == "string" or type(k) == "number") and type(v) ~= "function" and type(v) ~= "userdata" then 
                    lua = lua .. string.rep("    ", deep )
                    if type(k) == "string" then
                        k = "\"" .. k .. "\""
                    end
                    lua = lua .. "[" .. k .. "]=" .. serialize(v, deep + 1, filter) .. ",\n" 
                end
            end  
            lua = lua .. string.rep("    ", deep - 1)
            lua = lua .. "}" 
            filter[obj] = nil
        elseif t == "nil" then  
            return "nil"  
        else  
            error("can not serialize a " .. t .. " type.")  
        end  
        return lua  
    end

    local filter = {}

    return serialize(obj, deep, filter)
end  


local function writeBool(value)
    if value == 0 or value == nil then
        value = false
    elseif value == 1 then
        value = true
    end
    file:bool(value)
end

local function writeChar(value)
    file:char(value)
end

local function writeShort(value)
    file:short(value)
end

local function writeInt(value)
    file:int(value)
end

local function writeFloat(value)
    file:float(value)
end

local function writeString(value)
    if value ~= nil and type(value) == "string" then
        local idx = str2idx[value]
        if not idx then
            idx = #idx2str + 1
            idx2str[idx] = value
            str2idx[value] = idx
        end
        assert(idx < 0xFFFF / 2, "string index to big")
        file:short(idx - 1)
    else
        file:short(-1)
    end
end

local function writeColor(color)
    writeInt(tonumber(string.format("0x%s", color or "ffffffff")))
end

local function writeShorts(shorts)
    writeInt(#shorts)
    for _, value in ipairs(shorts) do
        writeShort(value)
    end
end

local function writeFloats(floats)
    writeInt(#floats)
    for _, value in ipairs(floats) do
        writeFloat(value)
    end
end

local function getSortedNames(t)
    local names = {}
    for name, _ in pairs(t) do
        names[#names + 1] = name
    end
    sort(names)
    return names
end


-------------------------------------------------------------------------------
-- name index
-------------------------------------------------------------------------------
local function initNameIndex()
    for i, bone in ipairs(data.bones) do
        bonename2idx[bone.name] = i - 1
    end

    for i, ik in ipairs(data.iks or {}) do
        ikname2idx[ik.name] = i - 1
    end

    for i, slot in ipairs(data.slots) do
        slotname2idx[slot.name] = i - 1
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
                        x = data.bones[bonename2idx[bonename] + 1].flipX,
                    })
                elseif flip.flipY then
                    bones[bonename].flipY = bones[bonename].flipY or {}
                    table.insert(bones[bonename].flipY, 1, {
                        time = 0,
                        x = data.bones[bonename2idx[bonename] + 1].flipY,
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
-- bones
-------------------------------------------------------------------------------
local function writeBones()
    local bones = data.bones
    writeInt(#bones)
    for i, bone in ipairs(bones) do
        writeString(bone.name)
        writeInt(bone.parent and bonename2idx[bone.parent] or -1)
        writeFloat(bone.x or 0)
        writeFloat(bone.y or 0)
        writeFloat(bone.scaleX or 1)
        writeFloat(bone.scaleY or 1)
        writeFloat(bone.rotation or 0)
        writeFloat(bone.length or 0)
        --writeBool(bone.flipX)
        --writeBool(bone.flipY)
        writeBool(bone.inheritScale or 1)
        writeBool(bone.inheritRotation or 1)
    end
end

-------------------------------------------------------------------------------
-- iks
-------------------------------------------------------------------------------
local function writeIKs()
    local iks = data.ik or {}
    writeInt(#iks)
    for i, ik in ipairs(iks) do
        writeString(ik.name)
        writeInt(#ik.bones)
        for _, bone in ipairs(ik.bones) do
            writeInt(bonename2idx[bone])
        end
        writeFloat(ik.mix or 0)
        writeInt(bonename2idx[ik.target])
        writeBool(ik.bendDirection)
    end
end

-------------------------------------------------------------------------------
-- slots
-------------------------------------------------------------------------------
local function writeSlots()
    local slots = data.slots
    writeInt(#slots)
    for i, slot in ipairs(slots) do
        writeString(slot.name)
        writeInt(bonename2idx[slot.bone])
        writeColor(slot.color)
        writeString(slot.attachment)
        writeBool(slot.additiveBlending or 0)
    end
end

-------------------------------------------------------------------------------
-- skins
-------------------------------------------------------------------------------
local function writeAttachment(attachment)
    writeString(attachment.name)
    if not attachment.type or attachment.type == "region" then
        writeChar(0)
        writeString(attachment.path)
        writeFloat(attachment.x or 0)
        writeFloat(attachment.y or 0)
        writeFloat(attachment.scaleX or 1)
        writeFloat(attachment.scaleY or 1)
        writeFloat(attachment.rotation or 0)
        writeFloat(attachment.width or 32)
        writeFloat(attachment.height or 32)
        writeColor(attachment.color)
    elseif attachment.type == "boundingbox" then
        writeChar(1)
        writeFloats(attachment.vertices)
    elseif attachment.type == "mesh" then
        writeChar(2)
        writeString(attachment.path)
        writeFloats(attachment.uvs)
        writeShorts(attachment.triangles)
        writeFloats(attachment.vertices)
        writeColor(attachment.color)
        writeInt(attachment.hull)
    elseif attachment.type == "skinnedmesh" then
        writeChar(3)
        writeString(attachment.path)
        writeFloats(attachment.uvs)
        writeShorts(attachment.triangles)
        writeFloats(attachment.vertices)
        writeColor(attachment.color)
        writeInt(attachment.hull)
    end
end

local function writeSkin(skin)
    local names = getSortedNames(skin or {})
    writeInt(#names)
    for i, name in ipairs(names) do
        local attachmentNames = getSortedNames(skin[name])
        writeInt(slotname2idx[name])
        writeInt(#attachmentNames)
        for i, attachmentName in ipairs(attachmentNames) do
            writeString(attachmentName)
            writeAttachment(skin[name][attachmentName])
        end
    end
end

local function writeSkins()
    local skins = data.skins or {}
    local names = getSortedNames(skins)
    writeInt(#names)
    for i, name in ipairs(names) do
        skinname2idx[name] = i - 1
        writeString(name)
        writeSkin(skins[name])
    end
end

-------------------------------------------------------------------------------
-- events
-------------------------------------------------------------------------------
local function writeEvents()
    local events = data.events or {}
    local names = getSortedNames(events)
    writeInt(#names)
    for i, name in ipairs(names) do
        writeString(name)
        writeInt(events[name].int or 0)
        writeFloat(events[name].float or 0)
        writeString(events[name].string)
        eventname2idx[name] = i - 1
    end
end

-------------------------------------------------------------------------------
-- animations
-------------------------------------------------------------------------------
local function calculateTimelineCount(animation)
    local count = 0

    for _, timelines in pairs(animation.slots or {}) do
        for _, _ in pairs(timelines) do
            count = count + 1
        end
    end

    for _, timelines in pairs(animation.bones or {}) do
        for _, _ in pairs(timelines) do
            count = count + 1
        end
    end

    for _, ffd in pairs(animation.ffd or {}) do
        for _, slots in pairs(ffd) do
            for _, slot in pairs(slots) do
                count = count + 1
            end
        end
    end

    for _, timelines in pairs(animation.ik or {}) do
        count = count + 1
    end

    if animation.events then
        count = count + 1
    end

    if animation.drawOrder then
        count = count + 1
    end

    return count
end

local function writeCurve(curve)
    if curve == "stepped" then
        writeChar(1)
    elseif type(curve) == "table" then
        writeChar(2)
        for _, v in ipairs(curve) do
            writeFloat(v)
        end
    else
        writeChar(0)
    end
end

local function writeAnimationSlots(slots)
    local slotnames = getSortedNames(slots)
    writeInt(#slotnames)
    for _, slot in ipairs(slotnames) do
        local timelinenames = getSortedNames(slots[slot])
        writeInt(slotname2idx[slot])
        writeInt(#timelinenames)
        for _, name in ipairs(timelinenames) do
            local timeline = slots[slot][name]
            writeChar(timelinetypes[name])
            writeInt(#timeline)
            assert(#timeline > 0)
            if name == "attachment" then
                for _, frame in ipairs(timeline) do
                    writeFloat(frame.time)
                    writeString(frame.name)
                end
            elseif name == "color" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time)
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
    local bonenames = getSortedNames(bones)
    writeInt(#bonenames)
    for _, bone in ipairs(bonenames) do
        local timelinenames = getSortedNames(bones[bone])
        writeInt(bonename2idx[bone])
        writeInt(#timelinenames)
        for _, name in ipairs(timelinenames) do
            local timeline = bones[bone][name]
            local type = timelinetypes[name]
            assert(type)
            writeChar(type)
            writeInt(#timeline)
            assert(#timeline > 0)
            if name == "rotate" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time)
                    writeFloat(frame.angle)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end  
            elseif name == "translate" or name == "scale" then
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time)
                    writeFloat(frame.x)
                    writeFloat(frame.y)
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end 
            elseif name == "flipX" or name == "flipY" then
                local field = name == "flipX" and "x" or "y"
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time)
                    writeBool(frame[field])
                end
            end
        end
    end
end

local function writeAnimationIKs(iks)
    writeInt(#iks)
    local names = getSortedNames(iks)
    for _, name in ipairs(names) do
        writeInt(ikname2idx[name])
        writeInt(#iks[name])
        assert(#iks[name] > 0)
        for i, frame in ipairs(iks[name]) do
            writeFloat(frame.time)
            writeFloat(frame.mix)
            writeBool(frame.bendPositive)
            if i < #timeline then
                writeCurve(frame.curve)
            end
        end
    end
end

local function writeAnimationFFDs(ffds)
    local ffdnames = getSortedNames(ffds)
    writeInt(#ffdnames)
    for _, ffd in ipairs(ffdnames) do
        local slotnames = getSortedNames(ffds[ffd])
        writeInt(skinname2idx[ffd])
        writeInt(#slotnames)
        for _, slot in ipairs(slotnames) do
            local timelinenames = getSortedNames(ffds[ffd][slot])
            writeInt(slotname2idx[slot])
            writeInt(#timelinenames)
            for _, name in ipairs(timelinenames) do
                local timeline = ffds[ffd][slot][name]
                writeString(name)
                writeInt(#timeline)
                assert(#timeline > 0)
                for i, frame in ipairs(timeline) do
                    writeFloat(frame.time)
                    writeInt(frame.offset or 0)
                    writeFloats(frame.vertices or {})
                    if i < #timeline then
                        writeCurve(frame.curve)
                    end
                end
            end
        end
    end
end

local function writeAnimationDraworder(draworder)
    writeInt(#draworder)
    for _, frame in ipairs(draworder) do
        local offsets = frame.offsets or {}
        writeInt(#offsets)
        for _, offset in ipairs(offsets) do
            writeInt(slotname2idx[offset.slot])
            writeInt(offset.offset)
        end
        writeFloat(frame.time)
    end
end

local function writeAnimationEvents(events)
    writeInt(#events)
    for _, frame in ipairs(events) do
        writeInt(eventname2idx[frame.name])
        writeFloat(frame.time)
        writeInt(frame.int or 0)
        writeFloat(frame.float or 0)
        writeString(frame.string)
    end
end

local function writeAnimation(animation)
    writeInt(calculateTimelineCount(animation))
    writeAnimationSlots(animation.slots or {})
    writeAnimationBones(animation.bones or {})
    writeAnimationIKs(animation.ik or {})
    writeAnimationFFDs(animation.ffd or {})
    writeAnimationDraworder(animation.drawOrder or {})
    writeAnimationEvents(animation.events or {})
end

local function writeAnimations()
    local animations = data.animations
    local names = getSortedNames(animations)
    writeInt(#names)
    for _, name in ipairs(names) do
        writeString(name)
        writeAnimation(animations[name])
    end
end

-------------------------------------------------------------------------------
-- strings
-------------------------------------------------------------------------------
local function writeStrings()
    writeInt(#idx2str)
    for _, value in ipairs(idx2str) do
        file:string(value)
    end
end

function sort(t, field)
    table.sort(t, function(a, b)
        if field then
            return a[field] < b[field]
        else
            return a < b
        end
    end)
end

function convert(json, filename)
    data = cjson.decode(json)
    file = spinebinary.new(filename)
    initNameIndex()
    trimSlotTimelines()
    makeupTimelines()
    writeBones()
    writeIKs()
    writeSlots()
    writeSkins()
    writeEvents()
    writeAnimations()
    writeStrings()
end