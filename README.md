# HDMI Mix for RK3588

HDMI-IN to HDMI-OUT mixer for RK3588.

libv4l2 for hdmirx, libdrm for hdmiout. DMABUF x4.

## why such a mess

RK3588 has special hardwares:

1. v4l2 hdmirx reports:
    * capabilities: MPLANES | STREAMING_IO
    * pixel format: NV12 on 4k, NV24 on 1080p
    * rk nv12/nv24 are composed of 2 cplanes, 1 mplanes (2 logical planes on 1 physical plane)
2. drm hdmiout reports:
    1. PRIMARY: NV* not supported
    2. OVERLAY/CURSOR NV* supported

## what that means

1. we can't use `drmModeSetCrtc` because this binds fb to primary plane, where pixel format NV* are not supported.
2. we only have a few overlay panels to work on. drm pageflip is not supported on non-primary planes.

## what this does

1. v4l2: setup multiple DMA buffer and export the fds as both userspace memory mapping and DMABUF fd.
2. drm: setup multiple framebuffers as mplanes/nv*
3. drm: find a compatible overlay panel
4. v4l2: on frame update call `drmModeSetPlane` to switch fb for panel.

## how it works

Pretty good!

Theoretically there could be some stuttering due to blocking calling `drmModeSetPlane`,

but in practice 4K@60Hz looks decent to me.

## How to use

1. kernel <= 6.1, large cma in kernel paramter like `cma=1024M`
2. install `libv4l2-dev`, `libdrm-dev` and other missing dev dependencies.
3. `make`
4. run `hdmimix`.
