/* liblxcapi
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "config.h"

#include <lxc/lxccontainer.h>

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define MYNAME "lxctest1"

static int destroy_container(void)
{
	int status, ret;
	pid_t pid = fork();

	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0) {
		execlp("lxc-destroy", "lxc-destroy", "-f", "-n", MYNAME, NULL);
		exit(EXIT_FAILURE);
	}

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		perror("waitpid");
		return -1;
	}

	if (ret != pid)
		goto again;

	if (!WIFEXITED(status))  { // did not exit normally
		fprintf(stderr, "%d: lxc-create exited abnormally\n", __LINE__);
		return -1;
	}

	return WEXITSTATUS(status);
}

static int create_container(void)
{
	int status, ret;
	pid_t pid = fork();

	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0) {
		execlp("lxc-create", "lxc-create", "-t", "busybox", "-n", MYNAME, NULL);
		exit(EXIT_FAILURE);
	}

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		perror("waitpid");
		return -1;
	}

	if (ret != pid)
		goto again;

	if (!WIFEXITED(status))  { // did not exit normally
		fprintf(stderr, "%d: lxc-create exited abnormally\n", __LINE__);
		return -1;
	}

	return WEXITSTATUS(status);
}

int main(int argc, char *argv[])
{
	struct lxc_container *c;
	int ret = 0;
	const char *s;
	bool b;
	char buf[201];
	int len;

	ret = 1;

	/* test a real container */
	c = lxc_container_new(MYNAME, NULL);
	if (!c) {
		fprintf(stderr, "%d: error creating lxc_container %s\n", __LINE__, MYNAME);
		ret = 1;
		goto out;
	}

	if (c->is_defined(c)) {
		fprintf(stderr, "%d: %s thought it was defined\n", __LINE__, MYNAME);
		goto out;
	}

	ret = create_container();
	if (ret) {
		fprintf(stderr, "%d: failed to create a container\n", __LINE__);
		goto out;
	}

	b = c->is_defined(c);
	if (!b) {
		fprintf(stderr, "%d: %s thought it was not defined\n", __LINE__, MYNAME);
		goto out;
	}

	len = c->get_cgroup_item(c, "cpuset.cpus", buf, 200);
	if (len >= 0) {
		fprintf(stderr, "%d: %s not running but had cgroup settings\n", __LINE__, MYNAME);
		goto out;
	}

	sprintf(buf, "0");
	b = c->set_cgroup_item(c, "cpuset.cpus", buf);
	if (b) {
		fprintf(stderr, "%d: %s not running but could set cgroup settings\n", __LINE__, MYNAME);
		goto out;
	}

	s = c->state(c);
	if (!s || strcmp(s, "STOPPED")) {
		fprintf(stderr, "%d: %s is in state %s, not in STOPPED.\n", __LINE__, c->name, s ? s : "undefined");
		goto out;
	}

	b = c->load_config(c, NULL);
	if (!b) {
		fprintf(stderr, "%d: %s failed to read its config\n", __LINE__, c->name);
		goto out;
	}

	if (!c->set_config_item(c, "lxc.uts.name", "bobo")) {
		fprintf(stderr, "%d: failed setting lxc.uts.name\n", __LINE__);
		goto out;
	}

	if (!lxc_container_get(c)) {
		fprintf(stderr, "%d: failed to get extra ref to container\n", __LINE__);
		exit(EXIT_FAILURE);
	}

	c->want_daemonize(c, true);
	if (!c->startl(c, 0, NULL)) {
		fprintf(stderr, "%d: %s failed to start\n", __LINE__, c->name);
		exit(EXIT_FAILURE);
	}

	sleep(3);

	s = c->state(c);
	if (!s || strcmp(s, "RUNNING")) {
		fprintf(stderr, "%d: %s is in state %s, not in RUNNING.\n", __LINE__, c->name, s ? s : "undefined");
		goto out;
	}

	len = c->get_cgroup_item(c, "cpuset.cpus", buf, 0);
	if (len <= 0) {
		fprintf(stderr, "%d: not able to get length of cpuset.cpus (ret %d)\n", __LINE__, len);
		goto out;
	}

	len = c->get_cgroup_item(c, "cpuset.cpus", buf, 200);
	if (len <= 0 || strncmp(buf, "0", 1)) {
		fprintf(stderr, "%d: not able to get cpuset.cpus (len %d buf %s)\n", __LINE__, len, buf);
		goto out;
	}

	sprintf(buf, "FROZEN");
	b = c->set_cgroup_item(c, "freezer.state", buf);
	if (!b) {
		fprintf(stderr, "%d: not able to set freezer.state.\n", __LINE__);
		goto out;
	}

	sprintf(buf, "XXX");
	len = c->get_cgroup_item(c, "freezer.state", buf, 200);
	if (len <= 0 || (strcmp(buf, "FREEZING\n") && strcmp(buf, "FROZEN\n"))) {
		fprintf(stderr, "%d: not able to get freezer.state (len %d buf %s)\n", __LINE__, len, buf);
		goto out;
	}

	c->set_cgroup_item(c, "freezer.state", "THAWED");

	c->stop(c);

    /* feh - multilib has moved the lxc-init crap */
#if 0
    goto ok;

	ret = system("mkdir -p " LXCPATH "/lxctest1/rootfs//usr/local/libexec/lxc");
	if (!ret)
		ret = system("mkdir -p " LXCPATH "/lxctest1/rootfs/usr/lib/lxc/");
	if (!ret)
		ret = system("cp src/lxc/lxc-init " LXCPATH "/lxctest1/rootfs//usr/local/libexec/lxc");
	if (!ret)
		ret = system("cp src/lxc/liblxc.so " LXCPATH "/lxctest1/rootfs/usr/lib/lxc");
	if (!ret)
		ret = system("cp src/lxc/liblxc.so " LXCPATH "/lxctest1/rootfs/usr/lib/lxc/liblxc.so.0");
	if (!ret)
		ret = system("cp src/lxc/liblxc.so " LXCPATH "/lxctest1/rootfs/usr/lib");
	if (!ret)
		ret = system("mkdir -p " LXCPATH "/lxctest1/rootfs/dev/shm");
	if (!ret)
		ret = system("chroot " LXCPATH "/lxctest1/rootfs apt-get install --no-install-recommends lxc");
	if (ret) {
		fprintf(stderr, "%d: failed to installing lxc-init in container\n", __LINE__);
		goto out;
	}
	// next write out the config file;  does it match?
	if (!c->startl(c, 1, "/bin/hostname", NULL)) {
		fprintf(stderr, "%d: failed to lxc-execute /bin/hostname\n", __LINE__);
		goto out;
	}
	//  auto-check result?  ('bobo' is printed on stdout)

ok:
#endif
	fprintf(stderr, "all lxc_container tests passed for %s\n", c->name);
	ret = 0;

out:
	if (c) {
		c->stop(c);
		destroy_container();
	}
	lxc_container_put(c);
	exit(ret);
}
