This code implements communication with the Eaton CKOZ-00/14
Communication stick (CI stick).  The CI stick needs to be added into
the network with CKOZ-00/13.

The code has been written without any kind of documentation from
Eaton, and may not follow their specifications.

The CKOZ-00/14 doesn't expose or know which devices hide behind the
datapoints.  It's the user's responsibility to send correct messages
to the datapoints; this code does no validation of messages sent.
However, the devices appear to ignore messages they don't understand.
MRF can export a datapoint to device map, but I have not added support
for that, as the benefits were limited.

A simple application for forwarding events to and from an MQTT server
is provided.  The application subscribes to the topics:

    "+/set/dimmer" (accepts values from 0-100)
    "+/set/switch" (accepts true or false)

and publishes on the topics:

    "[datapoint number]/get/dimmer" (value from 0-100)
    "[datapoint number]/get/switch" (true or false)

Sending `true` to topic `1/set/switch` will send a message to
datapoint 1 to turn on.  This will work for both switches and dimmers.
Sending the value `50` to `1/set/dimmer` will send a message to
datapoint 1 to set 50% dimming.  This will work only for dimmers.

Likewise, `1/get/dimmer` and `1/get/switch` will be set to the value
reported by the dimmer/switch, if and when datapoint 1 reports
changes.  Status reports are not routed in the xComfort network, so if
your CI stick is not able to reach all devices, these status messages
will be lost.

_WARNING: When I upgraded the CI stick to the "RF V2.08 - USB V2.05"
firmware, status reports from dimmers stopped working.  This was not
only an issue with this code, but with other applications as well.
Downgrading to "RF V1.08 - USB V1.04" resolved the issue._

This code was reverse engineered from a variety of sources, plus some
initial inspiration from <https://github.com/mifi/libxcomfort>.
