## NPLua - Networked Pico Lua for the Pi Pico 2W

Have you ever wanted to write scripts for the Raspberry Pi Pico 2W in Lua instead of the already excellent options of Python, C, or assembly? No? Well, here's your chance anyway!

NPLua is a Lua interpreter for the Pico 2 W with a Telnet console for uploading and running Lua scripts over the network. The console supports both IPv4 and IPv6.

> [!WARNING]
> Telnet is unencrypted and NPLua does not authenticate clients. Keep the console on a trusted network; do not expose its port directly to the Internet.

### Installation

NPLua targets Raspberry Pi Pico SDK 2.3.0 or newer and defaults to the `pico2_w` board.

#### 1 - Network information

Copy the local configuration template and edit it:

```sh
cp nplua_user.cmake.example nplua_user.cmake
```

`nplua_user.cmake` is ignored by Git. Wi-Fi credentials are still embedded in the resulting firmware, so treat built UF2/ELF files as sensitive.

#### 2 - Compile

If `PICO_SDK_PATH` points to an installed SDK, NPLua uses it. Otherwise CMake automatically fetches the pinned SDK release:

```sh
cmake -S . -B build
cmake --build build --parallel
```

The default `MinSizeRel` profile optimizes for the Pico's flash envelope. Pass
`-DCMAKE_BUILD_TYPE=RelWithDebInfo` when source-level debugging is more useful
than minimum firmware size.

Configuration can also be supplied directly:

```sh
cmake -S . -B build \
  -DNPLUA_WIFI_SSID="network-name" \
  -DNPLUA_WIFI_PASSWORD="network-password"
```

When changing SDK versions, use a fresh build directory rather than reusing an old CMake cache.

#### 3 - Flash the Result to Your Pi

Hold BOOTSEL while connecting the Pico over USB, then copy `build/nplua.uf2` to the `RPI-RP2` volume. After reboot, connect to the configured port (23 by default):

```sh
telnet <ipv4-or-ipv6-address> 23
```

### Using NPLua
Once you've got NPLua up and running, actually programming the Pico is simple! Type `lua` once connected via telnet to initialize input mode. Then you can either type in the Lua script manually (standard syntax applies) or copy/paste Lua programs over. Once you're finished writing/pasting your program, on a new line type `:done` and the program will immediately run, as long as there are no errors in your code.

If your program is a loop without any sort of break command, then it will keep going until you restart the device, which can be accomplished by typing `reboot` over the telnet connection or unplugging the device, at which point your Lua script will be wiped from memory. Lua scripts do not survive reboots, so if they're more than a few lines long it's recommended to write your Lua scripts in a text editor, save them somewhere on your PC, then copy/paste them over to the Pico.

If your program does not loop, it will execute and then you'll end up back at the `NPLua>` prompt, at which point you can either type `lua` to enter another script, which will overwrite the previous one, `quit` to quit, or `help` to list the commands.

As previously mentioned, there's also a `reboot` command, which will restart the device. This is handy in case you need to reflash the firmware for some reason, in which case you can just hold down the flash button and enter the `reboot` command instead of unplugging it. You do not need to reboot in order to write a new script, as entering a new script through the `lua` command overwrites your previous script in memory.

### Compatibility
The Pi Pico 2W is a great little device, but it doesn't have an operating system. Thus, standard Lua functions like `os.execute` and `os.exit` will not function in NPLua. Most other standard functions are supported. Try things out! That's one of the fun things about NPLua, you can play around with things without a bunch of reflashing of the hardware. There are also a couple of custom features implemented. `led(bool)` will turn the led on (true) or off (false). `sleep(number)` will initiate a sleep state for the entered number of seconds.

There's also a full suite of GPIO functions. `gpio.setMode(pin, number)` will initialize the pin as input or output (0 for input, 1 for output). `gpio.write(pin, bool)` will turn a particular pin on or off. `gpio.read(pin)` reads whether a pin is on or off. `gpio.toggle(pin)` toggles the state of a pin. NPLua permits the user-accessible Pico 2 W GPIOs 0-22 and 26-28.

### Bugs
Have you found a bug in NPLua? I'm not surprised, I'm sure there are plenty! Let me know, and I'll try to fix it.
