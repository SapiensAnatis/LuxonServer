#include <stdlib.h>
#include "luxon_server.h"

/* Implement the environment and WASI structures expected by the wasm2c instantiation interface.
   Both structs hold a pointer back to the primary module instance to access linear memory. */
struct w2c_env {
    w2c_luxon__server* instance;
};

struct w2c_wasi__snapshot__preview1 {
    w2c_luxon__server* instance;
};

int main(int argc, char** argv) {
    /* Initialize the WebAssembly runtime core infrastructure */
    wasm_rt_init();

    w2c_luxon__server server;
    struct w2c_env env;
    struct w2c_wasi__snapshot__preview1 wasi;

    /* Tie environment stubs to the server instance */
    env.instance = &server;
    wasi.instance = &server;

    /* Instantiate the WebAssembly module instance */
    wasm2c_luxon__server_instantiate(&server, &env, &wasi);

    /* Execute the module's WASI start entrypoint */
    w2c_luxon__server_0x5Fstart(&server);

    /* Free module state and overall WebAssembly runtime contexts */
    wasm2c_luxon__server_free(&server);
    wasm_rt_free();

    return 0;
}
