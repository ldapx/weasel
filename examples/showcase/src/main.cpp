#include <wsl/app.hpp>
#include <wsl/rsc/project_loader.hpp>

class showcase_app : public wsl::app {
public:
    showcase_app() : wsl::app("showcase", 1280, 720, 
#ifdef WSL_RESOURCE_PATH
        WSL_RESOURCE_PATH
#else
        "."
#endif
    ) {}

protected:
    void on_init() override {
        set_project_path("wslpro.json");
    }
};

int main(int argc, char** argv) {
    showcase_app app;
    return app.run();
}
