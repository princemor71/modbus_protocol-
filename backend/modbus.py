import streamlit as st
import paho.mqtt.client as mqtt
import json
import threading
import time

# MQTT CONFIG
BROKER = "broker.hivemq.com"
PORT = 1883

TOPIC_TX = "modbus/debug/tx"
TOPIC_RX = "modbus/debug/rx"
TOPIC_CMD = "modbus/command/read"

# Global data
latest_tx = ""
latest_rx = {}
connected = False

# MQTT CALLBACKS
def on_connect(client, userdata, flags, rc):
    global connected
    connected = True
    client.subscribe(TOPIC_TX)
    client.subscribe(TOPIC_RX)

def on_message(client, userdata, msg):
    global latest_tx, latest_rx

    data = msg.payload.decode()

    if msg.topic == TOPIC_TX:
        latest_tx = data

    if msg.topic == TOPIC_RX:
        try:
            latest_rx = json.loads(data)
        except:
            latest_rx = {"error": "Invalid JSON"}

# MQTT CLIENT SETUP
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

def mqtt_thread():
    client.connect(BROKER, PORT, 60)
    client.loop_forever()

# Start MQTT in background
threading.Thread(target=mqtt_thread, daemon=True).start()

# STREAMLIT UI
st.title("ESP32 Modbus Dashboard")

st.subheader("Connection Status")
st.write("Connected" if connected else "Connecting...")

# INPUT FORM
st.subheader("Send Modbus Command")

slave = st.number_input("Slave ID", value=1)
start = st.number_input("Start Address", value=0)
qty = st.number_input("Quantity", value=5)

if st.button("Send Command"):
    cmd = {
        "slave": int(slave),
        "start": int(start),
        "qty": int(qty),
        "func": 3
    }
    client.publish(TOPIC_CMD, json.dumps(cmd))
    st.success("Command Sent")

# DISPLAY TX
st.subheader("TX Data")
st.code(latest_tx)

# DISPLAY RX
st.subheader("RX Data")

if latest_rx:
    st.json(latest_rx)

    if "registers" in latest_rx:
        st.subheader("Registers")
        st.write(latest_rx["registers"])
else:
    st.write("No data received")

# AUTO REFRESH
time.sleep(1)
st.rerun()
