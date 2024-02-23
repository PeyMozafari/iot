##  Overview
 
My teammate and I, have developed an IoT sensor node that use the RIOT operating system. This specialized sensor node is designed for gathering environmental data, including temperature and pressure. 
The acquired data is securely sent using through MQTT secure protocol. The sensor node communicates with an Mqtt rsmb broker, and the Mosquitto client, configured with Mqtt Rsmb broker, facilitates a connection to AWS EC2. Following this, a Python client subscriber the targetted Mqtt topic fetches the data and stores it in InfluxDB.
The data is visible in simple visualized tables of InfluxDB, where the user can perform various operations of sorting and statistical methods.


##  Getting Started

###  Installation of Repositories

1. Clone the RIOT OS
git clone https://github.com/RIOT-OS/RIOT.git
2. Clone the iot repository:
git clone https://github.com/PeyMozafari/iot.git

###  Setting up and running iot application

1. Connect to Grenoble SSH Frontend
ssh <login>@grenoble.iot-lab.info

2. Start Experiment on IoT-Lab Test Bed
Launch an experiment with two M3 nodes and a single A8 Node.
Wait for a few minutes since experiment takes time to reach the "Running" state.
iotlab-experiment submit -n iot -d 60 -l 2,grenoble,m3 -l grenoble,a8

3. Setup Border Rounter on the first M3 Node
Source RIOT environment
source /opt/riot.source
Build border router firmware for M3 node with baudrate 500000
make ETHOS_BAUDRATE=500000 DEFAULT_CHANNEL=10 BOARD=iotlab-m3 -C RIOT/examples/gnrc_border_router clean all

4. Flash Border Router on the first M3 Node
Flash the border router to the first M3 node
iotlab-node --flash RIOT/examples/gnrc_border_router/bin/iotlab-m3/gnrc_border_router.elf -l grenoble,m3,<node-id>

5. Configure Border Router Network
Select an IPv6 prefix for the grenoble site (e.g., 2001:660:5307:3100::/64)
Configure the network of the border router on the M3 Node.
Setup a tap interface using ethos_uhcpd.py
sudo ethos_uhcpd.py m3-<node-id> tap4 2001:660:5307:3100::3/64

6. Setup MQTT Rsmb and Mosquitto Client on A8 Node. 
Connect to the SSH frontend in a new terminal, and ssh into A8 node
SSH into the A8 node
ssh root@node-a8-<node-id>

7. Get the global address of A8 Node
ifconfig

8. Initiate Mqtt rsmb
Enter the A8 shared directory
cd ~/A8
and then start the mqtt rsmb
./broker_mqtts config.conf

9. Start Mosquitto Client on A8
Update mosquitto.config and replace the IPv6 address with the IPv6 of AWS EC2 machine
Then start mosquitto client
root@node-a8-<node-id>:~/A8# mosquitto -c mosquitto.config

#### Configure iot Sensor Firmware

1. Start a new terminal, connect to SSH front end of grenoble site
2. Build the firmware for the iot sensor using A8 node's IPv6 address and tap-id i.e. 4
make DEFAULT_CHANNEL=10 SERVER_ADDR=<IPv6 address> EMCUTE_ID=station4 BOARD=iotlab-m3 -C . clean all

#### Flash the iot sensor firmware on another M3 node

iotlab-node --flash ./bin/iotlab-m3/iot.elf -l grenoble,m3,<node-id>

#### Connect to iot Sensor

Log into the iot sensor M3 node

nc m3-<node-id> 20000

and you will see iot sensor being connected to mqtt broker and sending the data

###  Cloud Setup

1. Create EC2 instance 

2. Assign IPv6 subnet: 
https://aws.amazon.com/blogs/networking-and-content-delivery/introducing-ipv6-only-subnets-and-ec2-instances/

3. Connect to EC2 instance using SSH and setup the mosquitto client:

sudo apt-get update
sudo apt-get install mosquitto
sudo apt-get install mosquitto-clients
sudo apt clean

4. Enable listener 1883 and allow anonymous

sudo nano /etc/mosquitto/mosquitto.conf

and then the below lines are to be added to the end of file

listener 1883
allow_anonymous true

5. Restart mosquitto client:

sudo service mosquitto restart

6. Setup docker:

sudo snap install docker

7. Setup Influxdb:

docker run --detach --name influxdb -p 8086:8086 influxdb:2.2.0

8. Enable the following ports to be open in security setting of EC2:

1883 mosquitto
8086 influxdb

9. Go to InfluxDB

<EC2-public-IPv4 Address>:8086

10. Setup the organization and create a bucket.

11. Start running the python file to subscribe to mqtt client

python3 mosquitto_sub.py

Now the python file will subscribe to the mqtt topic, fetch the data and will save it in influxdb.

