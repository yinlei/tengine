    var ws = new WebSocket("ws://localhost:8080/");
    ws.onmessage = function(evt) { console.log(evt.data); };
    ws.send("test");