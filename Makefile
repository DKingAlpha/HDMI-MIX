hdmimix: *.cpp *.hpp
	clang++ hdmimix.cpp v4l2.cpp drm.cpp -o hdmimix -I/usr/include/libdrm -lv4l2 -ldrm -lgbm -lEGL -lGL

clean:
	rm -f hdmimix
