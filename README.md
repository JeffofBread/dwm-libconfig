# dwm-libconfig

Do you want to completely up-end one of the core features of [suckless](https://suckless.org/) software? Well this is the patch for you.
dwm-libconfig is a patch for dwm that adds runtime configuration for dwm using [libconfig](https://hyperrealm.github.io/libconfig/). It
completely removes the need for the traditional `config.h`, replacing it with a configuration file called 'dwm.conf'. This means that to
adjust configuration values in dwm, you no longer need to recompile. Assuming you don't need to add new configuration options to the
parser you will never need to recompile dwm.

Some notes however, this is a backported featured from a very different project. Certain design choices or style are definitely a little
out of place for a [suckless](https://suckless.org/) application, but to be honest, I didn't to rewrite it completely just to "fit" better.
It is quite overbuild for the level of configuration dwm offers by default. That being said, I did design it to be quite robust and easily
extensible. With a little editing you can add a wide variety of new configuration values or edit the names or ranges of existing ones with
relative ease. If you would like to reach out, please do, I am more than happy to help. See [Reaching Out](#reaching-out) for contact info.

## Warning
I have tried my best to make this patch minimally invasive, but by its nature, is going to be quite invasive and will not work well with
many other patches without tweaking. This patch removes `config.h`, which many patches need.

## Requirements
The patch is based on dwm 6.6, so it is recommended to start there. You will need all the base dwm dependencies plus 
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

NOTE: Documentation in `dwm.conf` is not complete, I know. I have not finished all the documentation for the file yet, apologies.

dwm-libconfig will search for a configuration in a few places. It will first look for any configuration file passed from the CLI using 
`-c PATH`. If it finds no file or an invalid configuration file, it will continue. It will then search `~/.config/`, `~/.config/dwm/`.
If it is still unable to find a configuration, it will try and locate a backup of your latest successfully parsed configuration. 
This will be located at `~/.local/share/dwm/dwm_last.conf`. Note, this is NOT where YOU should save a file to. This is used as a backup
of your configuration, created and managed by dwm. Finally, if it can't find any user configurations, it will search in `/etc/dwm/`.
This is where a default, minimal configuration is saved. It will not be backed up, and purely exists as a fallback configuration. You
can edit this if you feel you need to, but it is probably best to leave it alone. Finally, should the parser completely fail and be
unable to locate a single valid configuration file, it will use an internal, hardcoded set of default values. 

Now about the configuration file itself. The example configuration provided with this repository (`dwm.conf`) contains most of the
documentation you should need. All elements in the file must follow the libconfig file syntax, read up on it here: 
 - https://hyperrealm.github.io/libconfig/libconfig_manual.html#Configuration-Files

Any issues with the syntax will cause parsing to fail, as libconfig is not very fault-tolerant when it comes to syntax. Warning,
older versions of libconfig do not support trailing commas. This means that parsing could fail if you place a comma after the final
element in a list. Please be careful about this, it can catch you off guard quite easily. Also, be careful with commas surrounding
the button/key binds. Libconfig supports multi-line strings, meaning if you forget a comma after a line of binds, libconfig will
combine the line you just wrote and the next together into one long bind, causing that keybind to likely fail to parse.

## Performance Impact
There is definitely a performance impact, but it is generally minimal. In my testing, even in extremely resource limited VM or emulated
systems, the time to parse a configuration is negligible. The longest time I found in my testing was around 400ms, with the rest of the
setup of dwm (mainly `setup()` and `scan()`) taking roughly 1s. During runtime, there are also some small performance losses, mainly
surrounding access time on elements in the keybind and button bind arrays. In dwm-libconfig, they are allocated on the heap, vs in
traditional dwm where they are on the stack. Combined with the loss of some compiler optimizations made in default dwm, you can run
into higher input latency on dwm keybind and button bind actions. Though, again, this is generally quite minimal. In the worst case
scenarios I found that it can add around 20-25ms of delay when accessing elements on opposite sides of the array in an extremely low
resource system or VM.

With all that said, real world performance losses are generally imperceptible. I have a relatively modern laptop and desktop PC, and both
see less than 3ms to parse a complex configuration, and keybind / button bind latency of less than .3ms at worst. And, lets be honest, if
you are here looking at this patch, optimizing down to the last atom is not likely your chief concern.

## TODOs
There are still a few things I want to adjust before releasing this as a proper patch:
- [ ] Complete documentation. I have about 50% of it already elsewhere, it just needs to be completed and adapted to this version of the parser
- [x] Fix the very clumsy logging. `print_log()` is just a placeholder macro to bridge the gap between my fully fledged logger and dwm's original source. It needs a little more polish.
- [x] If I can, I want to remove the need for most functions called by a bind to be made a non-const `Arg`. If I can solve this, it will reduce the codebase impact of the patch and reduce conflicts with other patches a little.
- [ ] Reduce codebase impact as much as possible to improve patch compatability

## Reaching Out
I am more than happy to help you out if you are having issues or just want to ask some questions about the patch. The best way is to reach
out on Discord @jeffofbread, or email me at jeffofbreadcoding@gmail.com.