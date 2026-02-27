# dwm-libconfig

Do you want to completely up-end one of the core features of [suckless](https://suckless.org/) software? Well this is the patch for you.
dwm-libconfig is a patch for dwm that adds runtime configuration for dwm using [libconfig](https://hyperrealm.github.io/libconfig/). It
(mostly) replaces the need to edit `config.h` for configuration changes with `dwm.conf`, a runtime parsed configuration file. This means that
to adjust configuration values in dwm, you no longer need to recompile. Assuming you don't need to change the behavior of the parser or dwm,
you don't need to recompile and re-install. Want to change your theme? A keybind? Bar position? Just edit the `dwm.conf` file and restart dwm,
simple as that. I highly recommend pairing dwm-libconfig with the [restartsig](https://dwm.suckless.org/patches/restartsig/) patch for easy 
hot-reloading of dwm. 

Some notes however, this is a backported featured from a different project. The code style and design choices are definitely a little
out of place for a [suckless](https://suckless.org/) application. It is also quite overbuild for the level of configuration dwm offers by
default, and is a bit "bloated" for that reason. It could definitely be slimmed down to a much lower line count at the sacrifice of
modularity or verbosity. That being said, I did design it to be quite robust, extensible, and modular. With a little editing you can add
just about anything you would like to the configuration relative ease, or slim down what already exists. If you would like to reach out,
please do, I am more than happy to help. See [Reaching Out](#reaching-out) for contact info.

## Requirements
The patch is based on dwm 6.8, so it is recommended to start there. You will need all the base dwm dependencies plus 
[libconfig](https://hyperrealm.github.io/libconfig/). You can find it in most distributions repositories without issue.

Here are a few common distros:
```bash
# Arch
sudo pacman -S libconfig
yay -S libconfig

# Debian / Ubuntu
sudo apt install libconfig-dev

# Fedora
sudo dnf install libconfig-devel

# openSUSE
sudo zypper install libconfig-devel
```

## Configuration
dwm-libconfig will search for a configuration in a few places (in this order):

1. Configuration file passed in through the CLI (`-c PATH`)
2. `$XDG_CONFIG_HOME/.config/dwm.conf` or `$HOME/.config/dwm.conf` if `$XDG_CONFIG_HOME` isn't defined
3. `$XDG_CONFIG_HOME/.config/dwm/dwm.conf` or `$HOME/.config/dwm/dwm.conf` if `$XDG_CONFIG_HOME` isn't defined

If those three paths fail or lead to invalid configurations, dwm will try and use fallback configurations. These are not intended to be
managed or edited directly by the user, and will not be backed up at the end of parsing. 

4. `$XDG_DATA_HOME/.local/share/dwm/dwm_last.conf` or `$HOME/.local/share/dwm/dwm_last.conf` if `$XDG_CONFIG_HOME` isn't defined
5. `/etc/dwm/dwm.conf`

The first of these two is a backup of the last successfully parsed user configuration. The second is a system-wide default configuration
installed alongside dwm during `make install`. In the event all configuration files fail to be found or contain major syntax errors
making parsing impossible, dwm will fall back to the default values defined in `config.h`.

Now about the configuration file itself. The example configuration provided with this repository (`dwm.conf`) contains most of the
documentation you should need. I recommend starting with this file and tweaking to fit your needs. All elements in the file must
follow the [libconfig](https://hyperrealm.github.io/libconfig/) file syntax, read up on it here: 

 - https://hyperrealm.github.io/libconfig/libconfig_manual.html#Configuration-Files

If there are major syntax errors, [libconfig](https://hyperrealm.github.io/libconfig/) will not be able to parse the file correctly, and parsing will fail, with dwm
falling back on the default configuration values specified in `config.h`. Minor syntax errors however, like an incorrect field in
a bind or a single setting, will not cause parsing to fail. In the case of a bind, that bind will simply be skipped, or in the case
of a setting, the default value from `config.h` will be used instead. 

## Performance Impact

There is definitely a performance impact, but it is quite minimal. In my testing, even in extremely resource limited VM or emulated
systems, the time to parse a configuration is negligible. The longest time I found in my testing was around 400ms, with the rest of the
setup of dwm (mainly `setup()` and `scan()`) taking roughly 1s. During runtime, there are also some small performance losses, mainly
surrounding access time on elements in the key and button bind arrays. In dwm-libconfig, they are not defined at compile time, vs in
traditional dwm where they are. Combined with the loss of compiler optimizations, this can lead to higher input latency on dwm key and
button bind actions. Though, again, this is generally extremely minimal. In the worst case scenarios I found that it can add around
20-25ms of delay when accessing elements on opposite sides of the array in low resource VM or emulated environments.

With all that said, real world performance losses are generally imperceptible. I have a relatively modern laptop and desktop PC, and both
see less than .75ms to parse a complex configuration, and key / button bind latency of less than .2ms at worst.

## TODOs
There are still a few things I want to adjust before releasing this as a proper patch:
- [ ] Complete the documentation.
- [ ] Add an extra diff(s) to support patches like [restartsig](https://dwm.suckless.org/patches/restartsig/) out of the box

## Reaching Out
I am more than happy to help you out if you are having issues or just want to ask some questions about the patch. The best way is to reach
out on Discord @jeffofbread, or email me at jeffofbreadcoding@gmail.com.
