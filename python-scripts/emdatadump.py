
#
# Quick and dirty data display script for the energy meter project
#
# Requires Python 2.7, Tkinter, and paho
#
# The command line takes 5 optional parameters:
#
# --host        host name       default mqtt
# --port        port number     default 1883
# --user        user name       default None
# --pw          password        default None
# --basetopic   base topic to use when sending commands and subscribing to status messages default /home/lab/acpowermon
#

__author__ = 'srodgers'

import paho.mqtt.client as mqtt
import json
import argparse
import Tkinter


#
# Convert unicode dict to dict of strings
#

def byteify(input):
    if isinstance(input, dict):
        return {byteify(key):byteify(value) for key,value in input.iteritems()}
    elif isinstance(input, list):
        return [byteify(element) for element in input]
    elif isinstance(input, unicode):
        return input.encode('utf-8')
    else:
        return input



# MQTT Connected callback

def on_connect(client, userdata, flags, rc):
    print("MQTT connected\n")

    # Subscribe to status topic
    client.subscribe(statustopic)

    # Request metering data from energy monitoring node
    client.publish(commandtopic, payload="{\"command\":\"query\"}")



#
# MQTT Message received callback
# MQTT to xPL path
#
def on_message(client, userdata, msg):
    data = byteify(json.loads(msg.payload))
    if 'urms' in data:
        urms.configure(text=data['urms'])
        irms.configure(text=data['irms'])
        pmean.configure(text=data['pmean'])
        smean.configure(text=data['smean'])
        qmean.configure(text=data['qmean'])
        freq.configure(text=data['freq'])
        powerf.configure(text=data['powerf'])
        pangle.configure(text=data['pangle'])
        kwh.configure(text=data['kwh'])

    # Re-request query data
    client.publish(commandtopic, payload="{\"command\":\"query\"}")








#
# Main code
#
if __name__ == '__main__':
    # Parse command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("--host",help="host name of mqtt server", default='mqtt')
    parser.add_argument("--port",type=int, help="port number of mqtt server",default=1883)
    parser.add_argument("--user",help="username", default=None)
    parser.add_argument("--pw",help="password", default=None)
    parser.add_argument("--basetopic", default='/home/lab/acpowermon')
    args = parser.parse_args()


    # Make command and status topics
    commandtopic = args.basetopic+'/command'
    statustopic = args.basetopic+'/status'

    # Instantiate MQTT client

    client = mqtt.Client()

    # Initialize MQTT callbacks
    client.on_connect = on_connect
    client.on_message = on_message

    # Set username and password if supplied
    if args.user is not None:
        client.username_pw_set(args.user, args.pw)

    # Connect to mqtt server
    client.connect(args.host, args.port, 60)
    print("MQTT Started\n")
    # Dedicate thread to mqtt client
    client.loop_start()
    root = Tkinter.Tk()

    #Set geometry
    root.geometry("800x200")
    root.columnconfigure(0, minsize=50)
    root.columnconfigure(1, minsize=50)

    #Set window title to base topic
    root.title(args.basetopic)

    # Set up display fields
    Tkinter.Label(master=root, text="Vrms" ).grid(row=0, column=1)
    urms = Tkinter.Label(master=root, width=10, anchor=Tkinter.E, text='', relief=Tkinter.SUNKEN)
    urms.grid(row=0, column=0)
    Tkinter.Label(master=root, text="Arms").grid(row=1, column=1)
    irms = Tkinter.Label(master=root, text='', width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    irms.grid(row=1, column=0)
    Tkinter.Label(master=root, text="kW").grid(row=2, column=1)
    pmean = Tkinter.Label(master=root, text='', width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    pmean.grid(row=2, column =0)
    Tkinter.Label(master=root, text="kVA").grid(row=3, column=1)
    smean = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    smean.grid(row=3, column =0)
    Tkinter.Label(master=root, text="kVAR").grid(row=4, column=1)
    qmean = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    qmean.grid(row=4, column =0)
    Tkinter.Label(master=root, text="Freq").grid(row=5, column=1)
    freq = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    freq.grid(row=5, column =0)
    Tkinter.Label(master=root, text="PF").grid(row=6, column=1)
    powerf = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    powerf.grid(row=6, column =0)
    Tkinter.Label(master=root, text="PH>").grid(row=7, column=1)
    pangle = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    pangle.grid(row=7, column =0)
    Tkinter.Label(master=root, text="kWh").grid(row=8, column=1)
    kwh = Tkinter.Label(master=root, text='',width=10, anchor=Tkinter.E, relief=Tkinter.SUNKEN)
    kwh.grid(row=8, column =0)

    # Enter Tk main loop

    root.mainloop()





