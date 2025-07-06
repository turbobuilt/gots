// Simple IP test without JSON.stringify
const ipv4 = "My IP is 192.168.1.1".match(/\b(?:\d{1,3}\.){3}\d{1,3}\b/);
console.log("ipv4 match:", ipv4);
if (ipv4) {
    console.log("Found IP:", ipv4[0]);
}