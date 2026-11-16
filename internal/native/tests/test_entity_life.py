"""Death classification keeps downed players visible and preserves range respawn."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
code=r'''
#include <assert.h>
#include <stdio.h>
#include "entity-life.h"
int main(void) {
 assert(!entity_observed_dead(1,0,0,1,1,1,3,0)); // Downed, not a final death.
 assert(!entity_observed_dead(1,0,0,0,0,1,1,0)); // Health replication before life state.
 assert(entity_observed_dead(1,0,0,1,1,1,2,0));
 assert(entity_observed_dead(1,100,1,0,0,1,3,1)); // Final death box wins.
 assert(!entity_observed_dead(1,100,1,1,1,1,2,0)); // Reused range actor healed.
 assert(entity_observed_dead(1,0,0,1,0,1,1,0)); // Range target still reports Alive.
 assert(entity_observed_dead(1,0,0,0,0,0,0,0)); // AI without PlayerState.
 assert(!entity_observed_dead(0,0,0,0,0,0,0,0));
 puts("PASS: downed players, replication transition, final death, death box, range death/respawn, unknown state and AI");
}
'''
with tempfile.TemporaryDirectory() as directory:
 src=Path(directory)/'life.c';binary=Path(directory)/'life';src.write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-I',str(root/'external/scanner'),str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
