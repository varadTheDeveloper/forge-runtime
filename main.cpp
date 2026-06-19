#include <stdio.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: forge <file.js>\n");
        return 1;
    }

    printf("Running: %s\n", argv[1]);

    FILE* file = fopen(argv[1], "r");

    if (!file) {
        printf("Cannot open file\n");
        return 1;
    }

    char buffer[4096];

    while (fgets(buffer, sizeof(buffer), file)) {
        printf("%s", buffer);
    }

    fclose(file);

    return 0;
}