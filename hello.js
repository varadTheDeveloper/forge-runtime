setTimeout(() => {
    print("hello");
}, 1000);
setInterval(() => {
    print("tick");
}, 1000);
const id = setTimeout(() => print("never"), 1000);

clearTimeout(id);
