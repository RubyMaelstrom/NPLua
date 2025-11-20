## NPLua - Networked Pico Lua for the Pi Pico 2W

Have you ever wanted to write scripts for the Raspberry Pi Pico 2W in Lua instead of the already excellent options of Python, C, or assembly? No? Well, here's your chance anyway!

NPLua is a Lua interpreter specifically for the 2W because the user utilizes a telnet connection to input Lua scripts for the Pico to run. This greatly simplifies things, as re-programming the device can be performed from across the room, across the network, or even across the world if you enjoy living dangerously and find a way to expose your Pico's port 23 to the internet.

### Installation

If you've flashed a Pi Pico before, the initial installation of NPLua is very simple.

#### 1 - Network information
Change the WIFI_SSID and WIFI_PASSWORD in CMakeLists.txt to your wifi SSID and password. This will be compiled as part of the program, and the Pi Pico 2W will automatically connect to your network once flashed and powered on.

#### 2 - Compile
NPLua requires the Pico SDK, and it is recommended to update the Lua folder to the newest version of Lua for maximum compatibility with the language. Once you have those installed, just build it as usual. Please consult your Pi Pico's documentation for information on how to compile C programs for the Pico 2W, as there's no secret sauce here.

#### 3 - Flash the Result to Your Pi
Hold the flash button down on the Pi, plug it in via USB, mount it, then drag/copy over the nplua.uf2 file that resulted from your build. The Pi will disconnect and reboot, then automatically connect to the network and set up the telnet server on port 23. Check your router for the Pi's IP address and type `telnet <ip> 23` into your terminal to connect, or use whatever telnet software you're most comfortable with to connect on port 23.

### Using NPLua
Once you've got NPLua up and running, actually programming the Pico is simple! Type `lua` once connected via telnet to initialize input mode. Then you can either type in the Lua script manually (standard syntax applies) or copy/paste Lua programs over. Once you're finished writing/pasting your program, on a new line type `:done` and the program will immediately run, as long as there are no errors in your code.

If your program is a loop without any sort of break command, then it will keep going until you restart the device, which can be accomplished by typing `reboot` over the telnet connection or unplugging the device, at which point your Lua script will be wiped from memory. Lua scripts do not survive reboots, so if they're more than a few lines long it's recommended to write your Lua scripts in a text editor, save them somewhere on your PC, then copy/paste them over to the Pico.

If your program does not loop, it will execute and then you'll end up back at the `NPLua>` prompt, at which point you can either type `lua` to enter another script, which will overwrite the previous one, `quit` to quit, or `help` to list the commands.

As previously mentioned, there's also a `reboot` command, which will restart the device. This is handy in case you need to reflash the firmware for some reason, in which case you can just hold down the flash button and enter the `reboot` command instead of unplugging it. You do not need to reboot in order to write a new script, as entering a new script through the `lua` command overwrites your previous script in memory.

### Compatibility
The Pi Pico 2W is a great little device, but it doesn't have an operating system. Thus, standard Lua functions like `os.execute` and `os.exit` will not function in NPLua. Most other standard functions are supported. Try things out! That's one of the fun things about NPLua, you can play around with things without a bunch of reflashing of the hardware. There are also a couple of custom features implemented. `led(bool)` will turn the led on (true) or off (false). `sleep(number)` will initiate a sleep state for the entered number of seconds.

There's also a full suite of GPIO functions. `gpio.setMode(pin, number)` will initialize the pin as input or output (0 for input, 1 for output). `gpio.write(pin, bool)` will turn a particular pin on or off. `gpio.read(pin, bool)` will, you guessed it, read in a pin is on or off. `gpio.toggle(pin)` will toggle the state of a pin.

### Bugs
Have you found a bug in NPLua? I'm not surprised, I'm sure there are plenty! Let me know, and I'll try to fix it.
