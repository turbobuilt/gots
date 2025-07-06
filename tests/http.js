const http = require('http');

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('yeshua love syou');
});

server.listen(8080, () => {
    console.log('Server running at http://localhost:8080/');
});