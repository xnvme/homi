/**
 * Test for the HOMI IPC.
 * 
 * Compile and test with 
 * gcc homi-ipc-test.c -o homi-ipc-test -lhomic -lxal; ./homi-ipc-test /dev/nvme0n1 /mnt/datasets/imagenetish/class496/class496_00000903.bin
 */
#include <stdio.h>
#include <stdlib.h>

#include <homic.h>
#include <libxal.h>

static int walker(struct xal *xal, struct xal_inode *inode, void *cb_args, int level) 
{
  printf("%*s- %s\n", level, "", inode->name);
  return 0;
}

static int
test_xal(const char *dev_uri, const char *path)
{
  struct xal_inode *inode = NULL;
  struct xal *xal = NULL;
  int err;

  err = homic_connect_xal((char *)dev_uri, &xal);
  if (err) {
    printf("FAIL homic_connect_xal(%s): %d\n", dev_uri, err);
    goto exit;
  }
  printf("OK homic_connect_xal(%s)\n", dev_uri);

  homic_xal_wait(xal);

  inode = xal_get_root(xal);
  if (!inode) {
    printf("FAIL xal_get_root(): NULL\n");
    err = -1;
    goto exit;
  }
  printf("OK xal_get_root(): ino=%" PRIu64 "\n", inode->ino);

  if (!path) {
    err = 0;
    goto exit;
  }

  err = xal_build_lookup_hashmap(xal);
  if (err) {
    printf("FAIL xal_build_lookup_hashmap(): %d\n", err);
    goto exit;
  }

  err = xal_get_inode(xal, (char *)path, &inode);
  if (err) {
    printf("FAIL xal_get_inode(%s): %d\n", path, err);
    goto exit;
  }
  printf("OK xal_get_inode(%s): ino=%" PRIu64 " size=%" PRIu64 "\n",
         path, inode->ino, inode->size);

  if (xal_inode_is_dir(inode)) {
    uint32_t nentries = inode->content.dentries.count;
    printf("Printing directory entries: (%d total)\n", nentries);
    xal_walk(xal, inode, walker, NULL);
  }

exit:
  return err;
}

int
main(int argc, char **argv)
{
  int err = 0;

  err = homic_connect("/run/homi/homi.sock");
  if (err) {
    printf("Failed connecting\n");
    goto exit;
  } else {
    printf("Connection success!\n");
  }

  err = test_xal(argv[1], argc > 2 ? argv[2] : NULL);

exit:
  printf("Closing connection and exiting ...\n");

  homic_disconnect();
  return err;
}
