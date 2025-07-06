function myCallback() {
  console.log("time has passed")
}

console.log(runtime.time.now());
runtime.timer.setTimeout(myCallback, 100);
