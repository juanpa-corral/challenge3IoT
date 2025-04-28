import paho.mqtt.client as mqtt
import json
import time

# Ubidots settings
UBIDOTS_BROKER = "industrial.api.ubidots.com"
UBIDOTS_PORT = 1883
UBIDOTS_TOKEN = "BBUS-zYBR0cbU0sj7oSahc1qs2qUxOa67cS"  # Verify this token
DEVICE_LABEL = "raspberry-pi"
TOPIC_UBIDOTS_DATA = f"/v1.6/devices/{DEVICE_LABEL}"
TOPIC_UBIDOTS_CONTROL = f"/v1.6/devices/{DEVICE_LABEL}/buzzer_control/lv"

# Local MQTT settings
LOCAL_BROKER = "localhost"
LOCAL_PORT = 1883
LOCAL_TOPIC_DATA = "sensors/data"
LOCAL_TOPIC_CONTROL = "sensors/control/buzzer"

# Callback when connected to local broker
def on_connect_local(client, userdata, flags, rc):
    print(f"Connected to local broker with code {rc}")
    if rc == 0:
        client.subscribe(LOCAL_TOPIC_DATA)
        print(f"Subscribed to {LOCAL_TOPIC_DATA}")
    else:
        print(f"Failed to connect to local broker, return code {rc}")

# Callback when connected to Ubidots broker
def on_connect_ubidots(client, userdata, flags, rc):
    print(f"Connected to Ubidots broker with code {rc}")
    if rc == 0:
        client.subscribe(TOPIC_UBIDOTS_CONTROL)
        print(f"Subscribed to {TOPIC_UBIDOTS_CONTROL}")
    else:
        print(f"Failed to connect to Ubidots broker, return code {rc}")

# Callback when message is received from local broker
def on_message_local(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        print(f"Received from local broker [{LOCAL_TOPIC_DATA}]: {data}")

        # Prepare data for Ubidots
        ubidots_data = {
            "distance": data["distance"] if data["distance"] is not None else 0,
            "precipitation_level": data["precipitation_level"],
            "state": data["state"],
            "buzzer_state": data["buzzer_state"]
        }

        # Publish to Ubidots
        ubidots_client.publish(TOPIC_UBIDOTS_DATA, json.dumps(ubidots_data))
        print(f"Sent to Ubidots [{TOPIC_UBIDOTS_DATA}]: {ubidots_data}")
    except Exception as e:
        print(f"Error forwarding to Ubidots: {e}")

# Callback when message is received from Ubidots (buzzer control)
def on_message_ubidots(client, userdata, msg):
    try:
        value = msg.payload.decode()
        print(f"Received from Ubidots [{TOPIC_UBIDOTS_CONTROL}]: {value}")

        # Forward to ESP32
        local_client.publish(LOCAL_TOPIC_CONTROL, value)
        print(f"Forwarded to ESP32 [{LOCAL_TOPIC_CONTROL}]: {value}")
    except Exception as e:
        print(f"Error forwarding control message: {e}")

# Callback when a message is published
def on_publish(client, userdata, mid):
    print(f"Message {mid} published successfully")

# Set up local MQTT client
local_client = mqtt.Client()
local_client.on_connect = on_connect_local
local_client.on_message = on_message_local
local_client.on_publish = on_publish
local_client.connect(LOCAL_BROKER, LOCAL_PORT, 60)

# Set up Ubidots MQTT client
ubidots_client = mqtt.Client()
ubidots_client.username_pw_set(UBIDOTS_TOKEN, "")
ubidots_client.on_connect = on_connect_ubidots
ubidots_client.on_message = on_message_ubidots
ubidots_client.on_publish = on_publish
ubidots_client.connect(UBIDOTS_BROKER, UBIDOTS_PORT, 60)

# Start loops
local_client.loop_start()
ubidots_client.loop_start()

# Keep the script running
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Disconnecting clients...")
    local_client.loop_stop()
    ubidots_client.loop_stop()
    local_client.disconnect()
    ubidots_client.disconnect()
    print("Disconnected")