CXX = cl
CXXFLAGS = /nologo /W3 /O2 /MD /EHsc /std:c++17
INCLUDES = /I"vendor\webui\include" /I"vendor\json\include"
LIBS = User32.lib Ole32.lib Advapi32.lib Shell32.lib vendor\webui\webui-2-static.lib
TARGET = app.exe
SRC = src\main.cpp src\core\Graph.cpp src\compiler\TaskflowGenerator.cpp src\sandbox\HostProcessDriver.cpp src\sandbox\DockerSandboxDriver.cpp src\sandbox\MicroVMSandboxDriver.cpp src\sandbox\SandboxManager.cpp src\sandbox\PipelineResumeEngine.cpp src\memory\AgentBlackboard.cpp src\memory\SemanticMemoryEngine.cpp src\mcp\McpClient.cpp src\mcp\McpRegistry.cpp src\project\ProjectManager.cpp src\pi\PiProjectScanner.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) /Fe$(TARGET) /link $(LIBS)

clean:
	del *.obj $(TARGET)
