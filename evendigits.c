#include <stdio.h>
int main() {
    int n = 0;
    printf("Enter Your Digit");
    scanf("%d", &n);

    for(int i = 2; i <= n; i += 2) {
        printf("%d", i);
        }

        printf("\n");

        return 0;
    }
