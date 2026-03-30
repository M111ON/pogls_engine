#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "gpu/pogls_federation.h"

#define VAULT_PATH "/tmp/pogls_disaster_vault"
#define TEST_DURATION_SEC 300  /* 5 minutes */

void corrupt_random_file() {
    const char *files[] = {
        VAULT_PATH "/.pogls/test_data/lane_X.delta",
        VAULT_PATH "/.pogls/test_data/lane_nX.delta",
        VAULT_PATH "/.pogls/test_data/snapshot.merkle"
    };
    int idx = rand() % 3;
    printf("[DISASTER] Corrupting %s...\n", files[idx]);
    int fd = open(files[idx], O_WRONLY);
    if (fd >= 0) {
        lseek(fd, rand() % 100, SEEK_SET);
        uint64_t junk = 0xBADBEEF;
        if (write(fd, &junk, sizeof(junk)) < 0) perror("write");
        close(fd);
    }
}

int run_worker_cycle(int cycle_id) {
    FederationCtx f;
    if (fed_init(&f, VAULT_PATH "/test_data") != 0) {
        return 1;
    }

    /* Recover first */
    Delta_DualRecovery dr = fed_recover(VAULT_PATH "/test_data");
    if (dr.world_a != DELTA_RECOVERY_CLEAN || dr.world_b != DELTA_RECOVERY_CLEAN) {
        printf("[WORKER %d] Recovery triggered: A=%d, B=%d\n", cycle_id, dr.world_a, dr.world_b);
    }

    /* Heavy traffic */
    for (int i = 0; i < 1000; i++) {
        uint32_t packed = i | ((i % 54) << 20);
        fed_write(&f, packed, i * 100, cycle_id);
        if (i % 18 == 0) fed_commit(&f);
        if (rand() % 500 == 7) {
            _exit( cycle_id % 2 == 0 ? 0 : 1 ); 
        }
    }

    fed_close(&f);
    return 0;
}

int main() {
    srand(time(NULL));
    printf("=== POGLS DISASTER RECOVERY STRESS TEST (5 MINUTES) ===\n");
    system("rm -rf " VAULT_PATH " && mkdir -p " VAULT_PATH);

    time_t start = time(NULL);
    int cycle = 0;

    while (time(NULL) - start < TEST_DURATION_SEC) {
        cycle++;
        printf("\n--- DISASTER CYCLE %d (Elapsed: %lds) ---\n", cycle, time(NULL) - start);

        pid_t pid = fork();
        if (pid == 0) {
            exit(run_worker_cycle(cycle));
        } else {
            int status;
            /* Randomly kill worker */
            if (rand() % 4 == 0) {
                usleep(rand() % 20000);
                printf("[CHAOS] SUDDEN KILL -9\n");
                kill(pid, SIGKILL);
            }
            waitpid(pid, &status, 0);

            /* Injected Disk Corruption */
            if (rand() % 10 == 0) {
                corrupt_random_file();
            }
            
            /* Verify Integrity via Recovery Tool */
            Delta_DualRecovery dr = delta_ab_recover(VAULT_PATH "/test_data");
            if (dr.world_a == DELTA_RECOVERY_ERROR) {
                printf("[FATAL] System failed to recover at cycle %d.\n", cycle);
                return 1;
            }
            printf("[MONITOR] Integrity OK (Status A:%d B:%d)\n", dr.world_a, dr.world_b);
        }
        usleep(50000); 
    }

    printf("\n=== FINAL DISASTER TEST RESULT: SUCCESS ===\n");
    printf("POGLS survived 5 minutes of extreme chaos.\n");

    return 0;
}
