-- A BUILT-IN SPEAKER NEVER TAKES THE SOUND FROM THE TV (#547, "Sounds plays
-- only from onboard speakers (Beelink ser9)"). The rest of MMagTech's rule
-- (#341) is in wireplumber.conf.d/60-cabinetos-sound-output.conf: the TV
-- first, a jack that senses the plug takes over, nothing else.
--
-- WirePlumber ranks an analog output above HDMI and passes over one only
-- when its connector reports nothing plugged in ("no"). A speaker inside
-- the case has no plug to sense, so it reports "unknown", is never passed
-- over, and outranks the TV: a Beelink SER9 (#547) and an HP EliteDesk 705
-- G4 (#416's report) played everything through it. A jack that cannot sense
-- a plug reports "unknown" too. Settings cannot tell these apart from
-- headphones: the speaker and the headphone jack are one output whose
-- connector changes when headphones go in.
--
-- So on a machine with HDMI or DisplayPort sound, any sound-card output whose
-- connector senses nothing is taken out of WirePlumber's list before it
-- picks the default. Left in: the TV's outputs, whatever they report; a
-- connector that senses something plugged in ("yes"), so headphones take
-- over and hand back as before; anything not on a sound card (Bluetooth,
-- Remote Play's own output); and a choice somebody made (Sunshine during a
-- stream, Steam's sound setting), which WirePlumber settles before this
-- runs. With the TV asleep, off or unplugged that can leave nothing, and the
-- console is silent, as one without a speaker always was (MMagTech,
-- 2026-10-21). A machine with no HDMI or DisplayPort sound is left alone.
--
-- This runs between two of WirePlumber's own steps, named below. If an
-- update renames them it may run after WirePlumber has picked, where it
-- changes nothing; it logs a warning then, and the speaker wins again.

cutils = require ("common-utils")

log = Log.open_topic ("s-cabinetos-sound-output")

-- The connector's kind ("hdmi", "speaker", "headphones" and so on). A
-- route's info is a count followed by key and value pairs.
function portType (route)
  local info = route.info
  if type (info) ~= "table" then
    return nil
  end
  for i = 2, #info - 1, 2 do
    if info [i] == "port.type" then
      return info [i + 1]
    end
  end
  return nil
end

-- HDMI and DisplayPort share the type; the name is for cards set up by a
-- UCM profile, which name their outputs HDMI1, HDMI2 and so on.
function isTVRoute (route)
  return portType (route) == "hdmi" or
      string.find (string.lower (route.name or ""), "hdmi", 1, true) ~= nil
end

-- Any sound card with an HDMI or DisplayPort output, connected or not.
function hasTVSound (devices_om)
  for device in devices_om:iterate {
    Constraint { "device.api", "=", "alsa" },
  } do
    for p in device:iterate_params ("EnumRoute") do
      local route = cutils.parseParam (p, "EnumRoute")
      if route and route.direction == "Output" and isTVRoute (route) then
        return true
      end
    end
  end
  return false
end

-- A sound-card output, not the TV's, whose connector does not report
-- something plugged in. The node's route is found as WirePlumber's own
-- lutils.haveAvailableRoutes () finds it.
function sensesNothing (node_props, devices_om)
  local card_profile_device = tonumber (node_props ["card.profile.device"])
  local device_id = node_props ["device.id"]
  if not card_profile_device or not device_id then
    return false
  end

  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }
  if not device or device.properties ["device.api"] ~= "alsa" then
    return false
  end

  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and route.device == card_profile_device then
      return not isTVRoute (route) and route.available ~= "yes"
    end
  end
  return false
end

SimpleEventHook {
  name = "cabinetos/leave-out-outputs-that-sense-nothing",
  after = { "default-nodes/find-selected-default-node",
            "default-nodes/find-stored-default-node" },
  before = { "default-nodes/find-best-default-node" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    if event:get_properties () ["default-node.type"] ~= "audio.sink" then
      return
    end

    -- find-best-default-node always leaves this behind
    if event:get_data ("selected-route-priority") ~= nil then
      log:warning ("ran after WirePlumber picked the output, so a built-in " ..
          "speaker can take the sound from the TV (#547)")
      return
    end

    if event:get_data ("selected-node") ~= nil then
      return
    end

    local available_nodes = event:get_data ("available-nodes")
    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    local devices_om = cutils.get_object_manager ("device")
    if not hasTVSound (devices_om) then
      log:debug ("no HDMI or DisplayPort sound; nothing left out")
      return
    end

    local kept = {}
    local left_out = false
    for _, node_props in ipairs (available_nodes) do
      if sensesNothing (node_props, devices_om) then
        log:info ("left out " .. tostring (node_props ["node.name"]) ..
            ": nothing senses its connector")
        left_out = true
      else
        table.insert (kept, Json.Object (node_props))
      end
    end

    log:debug (string.format ("%d outputs, %d left out",
        #available_nodes, #available_nodes - #kept))
    if left_out then
      event:set_data ("available-nodes", Json.Array (kept))
    end
  end
}:register ()
