# Arestrix - build do backend C++ no Windows (nmake + MSVC)
# Equivalente ao Makefile.linux (GNU make + GCC).
#
#   nmake -f Makefile                         # C++23 (padrao: /std:c++latest)
#   nmake -f Makefile STD=/std:c++20          # compatibilidade com C++20
#   nmake -f Makefile STD=/std:c++23preview   # VS 2022 17.13+ (flag explicita de C++23)
#
# O MSVC nao possui /std:c++23 estavel: /std:c++latest acompanha o rascunho mais recente.
STD = /std:c++latest

CXX = cl
CXXFLAGS = /nologo /W3 /O2 /MD /EHsc /Zc:__cplusplus $(STD)
INCLUDES = /I"vendor\webui\include" /I"vendor\json\include"
LIBS = User32.lib Ole32.lib Advapi32.lib Shell32.lib vendor\webui\webui-2-static.lib
TARGET = arestrix.exe
SRC = src\main.cpp src\core\Graph.cpp src\compiler\TaskflowGenerator.cpp src\sandbox\HostProcessDriver.cpp src\sandbox\DockerSandboxDriver.cpp src\sandbox\MicroVMSandboxDriver.cpp src\sandbox\SandboxManager.cpp src\sandbox\PipelineResumeEngine.cpp src\memory\AgentBlackboard.cpp src\memory\SemanticMemoryEngine.cpp src\mcp\McpClient.cpp src\mcp\McpRegistry.cpp src\project\ProjectManager.cpp src\pi\PiProjectScanner.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) /Fe$(TARGET) /link $(LIBS)
	@copy /Y $(TARGET) ..\$(TARGET)
	@copy /Y $(TARGET) app.exe

clean:
	del *.obj $(TARGET) app.exe ..\$(TARGET) 2>nul
