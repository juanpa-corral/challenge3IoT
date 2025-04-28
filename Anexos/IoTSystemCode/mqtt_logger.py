import paho.mqtt.client as mqtt
import csv
import json
from datetime import datetime

# MQTT settings
BROKER = "localhost"
PORT = 1883
TOPIC = "sensors/data"
CSV_FILE = "sensor_data.csv"

# Callback when connected to broker
def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe(TOPIC)

# Callback when message is received
def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with open(CSV_FILE, mode='a', newline='') as file:
            writer = csv.writer(file)
            writer.writerow([
                timestamp,
                data["distance"] if data["distance"] is not None else "INVALID",
                data["precipitation_level"],
                data["state"],
                data["buzzer_state"]
            ])
        print(f"Saved data: {data}")
    except Exception as e:
        print(f"Error processing message: {e}")

# Initialize CSV file with headers if it doesn't exist
try:
    with open(CSV_FILE, mode='x', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Timestamp", "Distance (cm)", "Precipitation Level (%)", "State", "Buzzer State"])
except FileExistsError:
    pass

# Set up MQTT client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Connect to broker and start loop
client.connect(BROKER, PORT, 60)
client.loop_forever()