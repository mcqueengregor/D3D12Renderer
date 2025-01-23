add_executable(Dx12Renderer WIN32
	main.cpp
	
	WinIncludes.h
	Helpers.h
	Dx12Headers/d3dx12.h
	
	CommandQueue.h
	CommandQueue.cpp
	)
	
target_link_libraries(Dx12Renderer
	d3d12.lib
	dxgi.lib
	dxguid.lib
	)
	