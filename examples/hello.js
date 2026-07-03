print("Hello Forge!");

setTimeout(() => {
    print("One second later!");
}, 1000);

let count = 0;

const id = setInterval(() => {
    print("Tick", ++count);

    if (count === 5) {
        clearInterval(id);
    }
}, 1000);