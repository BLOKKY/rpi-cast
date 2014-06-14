Raspberry Pi Video Cast Project - PiNM

- How to build:

git clone https://github.com/IS1/rpi-cast

cd rpi-cast

make

* Remember install path!

- How to run

[Go to install path]

./pcast

* ./pcast d:

  d means debug mode. Prints debug message.

* ./pcast

  do not print messages as possible.

- Auto startup

 sudo nano /etc/rc.local (You may use your favorite editor)

 Add this line before 'fi'

 [Install PATH]/pcast

 [Install PATH] is your install path. For example, install path is '/home/pi/rpi-cast', then you should add '/home/pi/rpi-cast/pcast' before 'fi'
