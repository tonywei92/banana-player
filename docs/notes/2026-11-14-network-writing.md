# Writing to network folders (SMB, FTP): what the libraries do

Files' Copy, Move, Rename and Delete on network folders (`src/xfer.cpp`, the backends' `makeDir`/`remove`/`rename`/
`put` in `net_smb.cpp` and `net_ftp.cpp`). Tested against a local FTP server (pyftpdlib) and Samba in Docker
(`dperson/samba`, a read-write and a read-only share) with `tools/bp-net` (`mkdir|rm|rmdir|mv|put`).

## libsmb2 6.2

- `smb2_open` with `O_CREAT|O_EXCL` is FILE_CREATE (a name taken: STATUS_OBJECT_NAME_COLLISION); `smb2_rename`
  sends ReplaceIfExists = 0. Neither ever writes over a file.
- A refused `mkdir`/`unlink`/`rmdir` gives a status of `-1073741790` (the NTSTATUS itself), while
  `smb2_get_nterror` holds a converted errno (`0xffffffcc`, -52), and the NTSTATUS is only in the error text
  ("Create failed with status -1073741790"). `describe()` reads it from there: without that, "access denied" read as
  "the connection was lost" (and the tree was dropped).
- A rename is within one share only; across shares the copy does it (copy, then delete).

## curl (FTP)

- QUOTE commands (`MKD`, `DELE`, `RMD`, `RNFR`/`RNTO`) count any reply but 2xx as a failure: `RNFR`'s 350 too, so it
  goes as `*RNFR` (failure ignored; `RNTO`'s answer says if it worked).
- A reused connection stays in the folder of its last transfer, so a relative path in a command lands in the wrong
  place (`/rw/rw/x`). Commands use whole paths from the login's folder (`CURLINFO_FTP_ENTRY_PATH`, read once).
- `RNTO` replaces a file on most servers: the name is checked free (the folder listed) first.

## Read only

Nothing tells both protocols' write permission the same way, so the listing of the folder the browser opens puts an
empty `.bp-write-test` and removes it (`NetWorker`, `probe`). A refusal (NoAccess) or any failure: read only.

## Playback over a dropped network

- A network failure (anything but "the file can't be opened") is `Player::errorNet`; the session's CantPlay then has
  reason `net` and never skips (each next file failed the same way, one after another, after its timeout). Play on
  that error reloads it at the session's position (reducer; `SessionHost` HhLoad reopens a held file in error).
- `MPV_EVENT_END_FILE` of the file before (reason stop) comes once the next is the current one: it took the next
  one's stream failure away, so only an error's or an end's END_FILE takes it. An error END_FILE of an entry other than
  the one last started is an earlier try's, ignored. And a retry's reload runs after the session's pass, so CantPlay
  also waits for a load of the player's own (`Player::loads()`).
