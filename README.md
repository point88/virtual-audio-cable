# Virtual-Audio-cable
This repository is macOS virtual audio loopback driver.
You should be able to re-compile the driver on you Mac if you have Xcode. 
This is freshly compiled by Xcode on my Intel-Mac running MacOS 10.15 "Catalina" 
and is the same you can find somewhere deep in the Xcode folder.

This is the school assignment for this summar semester.
I have been working on this repository for 1 month.
This is the assignment repository.
So feel free to use for any case, But you should customize this for your purpose.


#How to build
1. create xcode project.
2. import VACdummy.c file
3. run build

#How to install
1. copy driver files to library directory.
	cp -R VAC.driver /Library/Audio/Plug-Ins/HAL/
	
2. Restart CoreAudio with the terminal command:
	sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod

#How to contact to me
	ferrepoint88@gmail.com