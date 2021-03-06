BitsFS  -  ex uno plura
=======================

BitsFS is a FUSE-based virtual file system which provides a single virtual big
file which will be stored as a lot of small physical files representing equally
sized chunks of the big file. This can be used to simulate block-level access on
a storage system that only allows file-level access.


Usage Scenario
--------------

Suppose you want to backup data to a remote system you do not trust, for example
an FTP or CIFS share or one of the many "remote storage" providers that
currently exist. Some might even provide a client-side application that encrypts
your data on your local machine before transmitting it to them. However, since
these applications are usually closed-source, you have to trust it to contain no
backdoor or something like that - which still does not solve your trust problem.
What you want is to encrypt the data using standard tools like dmcrypt, which
unfortunately only works on block devices.

You could create a crypto container file of, say, 200 MB, use losetup to create
a block device out of it, run dmcrypt on this block device and backup your data
to it. However, since you will probably not be able to use rsync to send your
container file to the remote machine, you will have to upload the whole file
from scratch every time you want to propagate your changes. This is not
particularly performant.

You could mount the remote filesystem and then lay EncFS over it to encrypt the
files written to it transparently. However, EncFS leaks information: File size,
modification time, filename length, directory structures. And if the underlying
filesystem is non-POSIX, you lose permissions, ACLs and other metadata.

You could use a tool like Duplicity to create encrypted backups to the machine.
However, you might be accustomed to rdiff-backup's "reverse incremental" backup
technique which Duplicity does not provide.

Enter BitsFS.

With BitsFS, you would mount the remote file system locally and then instruct
BitsFS to create a (possibly large) virtual container file for you, using the
remote file system as its actual storage backend. Since BitsFS writes data on
demand only, this process is instantaneous. Then, losetup, dmcrypt, mkfs on
your virtual file to make it usable. Finally, start writing backups to it!


Current Status
--------------

This is an alpha version. It basically works, but not all features are present.
Additionally, the only way to configure it is by editing the source code.

**PLEASE DO NOT YET TRUST BITSFS TO STORE YOUR DATA.**
It may crash, write data the wrong way and later not be able to read it back, or
might even remove data that is completely unrelated. *USE AT YOUR OWN RISK.*

That said, the chances of deleting unrelated data are quite low. Write and read
tests we did worked flawlessly, but we do not trust it enough to store our
backups yet.

BitsFS development is currently (April 2010) being paused, but should continue
in May. Testing and reporting things is encouraged; writing large patches
however not yet.


Compiling
---------

`make`. The `bitsfs` binary will be generated. If this does not work, get in
touch with us.


Basic Usage
-----------

	mount remote:/filesystem /whereever
	mkdir /whereever/bitsfs
	cd /whereever/bitsfs
	bitsfs /mnt/bitsfs
	losetup -f /mnt/bitsfs/bits_file
	cryptsetup luksCreate ... /dev/loopX
	cryptsetup luksOpen /dev/loopX cryptbits
	mk*fs /dev/mapper/cryptbits
	mount /dev/mapper/cryptsbits /mnt/backups
