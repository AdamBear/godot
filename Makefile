
.PHONY: bin/libgodot.linuxbsd.opt.tools.64.llvm.so

bin/libgodot.linuxbsd.opt.tools.64.llvm.so: 
	scons p=linuxbsd -j`nproc` target=release_debug use_llvm=yes library_type=shared use_lld=yes module_webm_enabled=no use_static_gcc=yes optimize=size debug_symbols=yes
	cp -f bin/libgodot.linuxbsd.opt.tools.64.llvm.so ${MIX_APP_PATH}/priv/