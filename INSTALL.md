# djvudict

*djvudict* is an ad-hoc project for *minidjvu-mod*. I mean you shall copy its sources to minidjvu-mod sourcecode, apply a few patches and build minidjvu-mod as usual. After that *minidjvu* will compile a one more executable named *djvudict*.

# Step by step:
1. Get *minidjvu-mod* sources (you may also check if it's buildable in your system)
2. Copy content of `djvudict/tools` folder to tools subfolder of *minidjvu-mod*
3. Apply `changes.diff` patch to enable their compilation and to let code access some minidjvu-mod private functions
5. Build *minidjvu-mod* as usual

# Example
(I'm getting debianized minidjvu-mod sources from github)
```
# get to temp dir (you may use another)
cd /tmp
# get minidjvu-mod and djvudict sources
git clone https://github.com/trufanov-nok/minidjvu-mod.git
git clone https://github.com/trufanov-nok/djvudict

cd ./minidjvu-mod

# copy djvudict sourcecode
cp ../djvudict/tools/* ./tools/

# apply patches
git apply ../djvudict/changes.diff

# build minidjvu-mod as described in its README
autoreconf --install
./configure
make
```

You shall find `djvudict` binary in the same folder as `minidjvu-mod`
