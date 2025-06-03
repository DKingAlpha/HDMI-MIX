hdmimix: hdmimix.cpp v4l2.cpp v4l2.hpp drm.cpp drm.hpp
	clang++ hdmimix.cpp v4l2.cpp drm.cpp -o hdmimix -I/usr/include/libdrm -lv4l2 -ldrm

clean:
	rm -f hdmimix
