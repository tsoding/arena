let wasm;

(async () => {
    const log = document.getElementById("log");
    const url = "build/expr.wasm";
    const wasm = await WebAssembly.instantiateStreaming(fetch(url), {
        "env": {
            "platform_write": (buf, len) => {
                const text = new TextDecoder().decode(new Uint8Array(wasm.instance.exports.memory.buffer, buf, len));
                log.innerText += text;
            },
        }
    });
    console.log(wasm);
    wasm.instance.exports.main();
})()
