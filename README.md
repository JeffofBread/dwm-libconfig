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
see less than 30ms to parse a complex configuration, and keybind / button bind latency of less than 3ms at worst. And, lets be honest, if
you are here looking at this patch, optimizing down to the last atom is not likely your chief concern.

## TODOs
There are still a few things I want to adjust before releasing this as a proper patch:
- [ ] Complete documentation. I have about 50% if it already elsewhere, it just needs to be completed and adapted to this version of the parser
- [ ] Fix the very clumsy logging. `print_log()` is just a placeholder macro to bridge the gap between my fully fledged logger and dwm's original source. It needs a little more polish.
- [ ] If I can, I want to remove the need for most functions called by a bind to be made a non-const `Arg`. If I can solve this, it will reduce the codebase impact of the patch and reduce conflicts with other patches a little.

## Reaching Out
I am more than happy to help you out if you are having issues or just want to ask some questions about the patch. The best way is to reach
out on Discord @jeffofbread, or email me at jeffofbreadcoding@gmail.com.