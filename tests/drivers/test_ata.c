#include <assert.h>
#include "ata.h"

int main(void) {
    uint8_t buf[512];
    // 1. Should fail before ata_init()
    int r = ata_read_sectors(0, 0, 1, buf);
    assert(r < 0 && "Read should fail before driver init");

    // 2. Initialize driver
    r = ata_init();
    assert(r == 0 && "ata_init should succeed");

    // 3. Now read should succeed (returns 0 for stub implementation)
    r = ata_read_sectors(0, 0, 1, buf);
    assert(r == 0 && "Read should succeed after init");

    // 4. Write should also succeed after init
    r = ata_write_sectors(0, 0, 1, buf);
    assert(r == 0 && "Write should succeed after init");

    return 0;
}
