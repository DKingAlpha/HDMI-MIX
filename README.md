# HDMI Mix for RK3588

HDMI-IN to HDMI-OUT mixer for RK3588.

* libv4l2 for hdmirx
* libdrm for hdmiout. DMABUF x4.
* gbm/egl/gles3.1 for rendering
* imgui for UI.

## why such a mess

RK3588 has special hardwares:

1. v4l2 hdmirx reports:
    * capabilities: MPLANES | STREAMING_IO
    * pixel format: NV12 on 4k, NV24 on 1080p
    * rk NV12/NV24 are composed of 2 cplanes, 1 mplanes (2 logical planes on 1 physical plane)
2. drm hdmiout reports:
    1. PRIMARY: NV* not supported
    2. OVERLAY/CURSOR: NV* supported

## what that means

1. we can't use `drmModeSetCrtc` for HDMI-IN DMABUF because this API binds fb to primary plane, where pixel format NV* are not supported.
2. we only have a few overlay panels to render out HDMI-IN DMABUF. drm pageflip is not supported on non-primary planes.
3. primitively, we can use bind a dumb buffer to 'primary' panel as canvas, to render floating UI (a real overlay), and change zpos to top it. but we only have a raw dumb buffer to manually draw on. This is dumb.
4. alternatively, we can go with offline rendering, with GBM/EGL/GLES3.1, and import the surface buffer to DRM as a framebuffer, then top it. With hardware acceleration and OpenGL support, we can render GUI softwares on it!

## what this does

1. v4l2: setup multiple DMA buffer and export the fds as both userspace memory mapping and DMABUF fd.
2. drm: setup multiple framebuffers as mplanes/nv*
3. drm: find a compatible overlay panel
4. v4l2: on frame update call `drmModeSetPlane` to switch fb for panel.
5. gbm: create a buffer and pass to egl to create a surface, and import gbm_bo to DRM as a framebuffer.
6. egl: find proper config, set up context with the best GL version.
7. imgui: render UI with gl* API, it goes to "current" context and thats the one we just set up.

## Performance

Smooth on 4K@60Hz!

Refresh rate with imgui. Input FPS should be 60.01Hz

```
-----------------------------
FPS: 60.001
Jitter: 0%
MaxDev: 0.0106587ms
StdDev: 0.00329361ms

-----------------------------
FPS: 60.0008
Jitter: 0%
MaxDev: 0.0156167ms
StdDev: 0.00524ms

-----------------------------
FPS: 60.0007
Jitter: 0%
MaxDev: 0.0129917ms
StdDev: 0.0028702ms
```

## How to use

1. kernel <= 6.1, large cma in kernel paramter like `cma=1024M`
    * hdmirx is broken on kernel 6.1x !
2. install `libv4l2-dev`, `libdrm-dev` and other missing dev dependencies.
3. clone imgui to current directory:
4. `make`
5. run `hdmimix`.
