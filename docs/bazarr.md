# Using LAPSE with Bazarr

Bazarr can use LAPSE instead of ffsubsync to sync the subtitles it downloads. This guide shows how to set it up.

Most people run Bazarr in Docker, so that comes first. If Bazarr is installed straight on your computer, jump to [Without Docker](#without-docker).

## With Docker

You run LAPSE as its own container next to Bazarr. Bazarr sends it one subtitle at a time and gets the fixed subtitle back.

Here is a compose file with both. It uses the linuxserver Bazarr image. Change the paths and the `PUID` and `PGID` to match your own setup.

```yaml
services:
  bazarr:
    image: lscr.io/linuxserver/bazarr:latest
    container_name: bazarr
    restart: unless-stopped
    ports:
      - 6767:6767
    volumes:
      - ./bazarr:/config
      - /srv/media/movies:/movies
      - /srv/media/tv:/tv
    environment:
      - PUID=1000
      - PGID=1000

  lapse:
    image: ghcr.io/schwponaco-org/lapse:latest
    container_name: lapse
    restart: unless-stopped
    volumes:
      - ./lapse:/data
      - /srv/media/movies:/movies:ro
      - /srv/media/tv:/tv:ro
    environment:
      - SCAN=0
      - PUID=1000
      - PGID=1000
```

Then run `docker compose up -d`.

What the LAPSE part does:

- `SCAN=0` stops LAPSE from going through your library on its own. It only works when Bazarr asks.
- The media is mounted read only. LAPSE never writes to it.
- `/data` is where LAPSE keeps its database and its cache.
- `PUID` and `PGID` are optional. Use the same user as Bazarr.
- LAPSE listens on port 8080 inside the container. Bazarr reaches it over the compose network, so you do not need to publish the port.

You need LAPSE 2.5.1 or newer. Older versions do not have the part that Bazarr talks to.

Now open Bazarr and go to Settings, Subtitles, Processing, and open **Audio Synchronization**. Set:

- **Enable Automatic Subtitles Audio Synchronization:** on
- **Synchronization Engine:** LAPSE
- **LAPSE URL:** `http://lapse:8080`

Press **Test**. Bazarr shows "Connected to LAPSE" and the version when it works. Then save.

If Bazarr uses a proxy, add `lapse` (or the host of your LAPSE URL) to **Ignored Addresses** under Settings, General, Proxy. Otherwise Bazarr cannot reach it.

### The same path rule

LAPSE has to find the video at the same path that Bazarr uses. Bazarr sends the path, and LAPSE opens that exact path inside its own container.

For example, if Bazarr sees a movie at `/movies/Heat (1995)/Heat.mkv`, then LAPSE needs the same folder at `/movies`. In the compose file above both containers have `/srv/media/movies:/movies`, so the path means the same thing in both. If LAPSE had it at `/media/movies` instead, it would not find the file.

If Sonarr and Radarr use other paths than Bazarr, that is fine. LAPSE needs the path Bazarr itself uses to open the file, after Bazarr's path mappings.

When the path is wrong, Bazarr logs this:

```
LAPSE could not find the video. Mount your media in the LAPSE container at the same path as in Bazarr.
```

The 404 behind that message means the video was not there. Fix the mounts so both containers use the same paths, and run `docker compose up -d` again.

### Bazarr in another compose file, or on the host

The name `lapse` only works when both containers are on the same Docker network. If your Bazarr is somewhere else, publish the LAPSE port by adding this to the `lapse` service:

```yaml
    ports:
      - 8080:8080
```

Then use the IP address of the machine in Bazarr, for example `http://192.168.1.20:8080`. If Bazarr uses host networking, `http://localhost:8080` works too.

There is no login on this port, so keep it on your own network.

## Without Docker

If Bazarr is installed directly on Linux, Windows or macOS, run the `lapse` program from Bazarr.

1. Open the [releases page](https://github.com/Schwponaco-org/lapse/releases) and download the file for your system:

   | System | File |
   |---|---|
   | Linux x86-64 | `lapse-linux-amd64.tar.gz` |
   | Linux ARM64 | `lapse-linux-arm64.tar.gz` |
   | Alpine Linux x86-64 | `lapse-linux-musl-amd64.tar.gz` |
   | Alpine Linux ARM64 | `lapse-linux-musl-arm64.tar.gz` |
   | macOS Apple silicon | `lapse-macos-arm64.tar.gz` |
   | macOS Intel | `lapse-macos-x86_64.tar.gz` |
   | Windows x64 | `lapse-windows-x64.zip` |

   The musl files are for Alpine and other musl systems. Everyone else on Linux uses the first two.

   Download the archive. The single `lapse-...` file next to it has no speech model, and LAPSE needs the model for its best results.

2. Check the download. The release has a `SHA256SUMS` file. Put it in the same folder as your download and run this with your own file name:

   ```bash
   grep lapse-linux-amd64.tar.gz SHA256SUMS | sha256sum -c       # Linux
   grep lapse-macos-arm64.tar.gz SHA256SUMS | shasum -a 256 -c       # macOS
   ```

   On Windows, run this and compare the result with the line for your file in `SHA256SUMS`:

   ```
   certutil -hashfile lapse-windows-x64.zip SHA256
   ```

3. Unpack it and keep all the files together in one folder. On Linux and macOS, check that it runs:

   ```bash
   tar xf lapse-linux-amd64.tar.gz
   lapse-linux-amd64/lapse --version
   ```

4. Tell Bazarr where it is. In Settings, Subtitles, Processing, **Audio Synchronization**, set **Synchronization Engine** to LAPSE and either:

   - fill in **LAPSE Executable Path** with the full path, such as `/opt/lapse/lapse` or `C:\lapse\lapse.exe`. This always works, or
   - put the unpacked folder on your `PATH` (or a symlink to `lapse` in a folder that is, such as `/usr/local/bin`) and leave **LAPSE Executable Path** empty. If Bazarr runs as a service (systemd or a Windows service), it can have its own `PATH`, so this may not work there.

   Leave **LAPSE URL** empty. Press **Test** to see the version, then save.

Which one Bazarr uses: it uses **LAPSE URL** if you filled it in. If not, it uses **LAPSE Executable Path**. If that is empty too, it looks for `lapse` on the `PATH`. If it finds none of them, it logs a warning and syncs with ffsubsync instead. It does the same when **LAPSE URL** is filled in but the container cannot be reached.

## What Bazarr does with the result

LAPSE rates every sync as solid, unsure or nothing. Bazarr only writes the solid ones. With the other two, your subtitle stays exactly as it was, and the reason shows up in Bazarr's log.

Some things to know:

- Bazarr's **score threshold** still decides which subtitles get synced.
- The ffsubsync offset and quality settings are hidden when LAPSE is the engine. LAPSE checks every sync itself.
- Progress and cancel work in Bazarr's jobs list just like with ffsubsync.

LAPSE uses up to 8 CPU cores for one sync. **Concurrent Syncs** in Bazarr multiplies that, so on a small machine keep it at 1.

LAPSE saves what it learns about a video, so a second sync of the same video is faster. With Docker this is in `/data/cache` in the LAPSE container. Without Docker it is in `cache/lapse` in Bazarr's config folder, unless you set `LAPSE_CACHE` to another place.

## If something goes wrong

Bazarr's log is under System, Logs. Look for lines that start with `BAZARR LAPSE`.

**The Test button shows an error.**

- "Could not get the version from <url>: ...": Bazarr cannot reach the LAPSE container. Check that the container is running, that the URL is right, and that both are on the same Docker network. If they are not, publish the port as described above.
- "LAPSE executable not found (...)": the path is wrong, the file is not executable, or `lapse` is not on the `PATH`.
- "Could not run <path> --version: ...": the file is there but does not start. Check that it is the right one for your system.

**"LAPSE could not find the video" (404).** The same path rule above. Make both containers see the media at the same path.

**"LAPSE is not available (...), using ffsubsync instead".** Bazarr could not reach LAPSE, or found no `lapse` program. That sync is done by ffsubsync, so nothing is lost. Run the Test button to see why.

**"LAPSE left these subtitles untouched (...)".** LAPSE was not sure enough to change the subtitle. The verdict and the reason are in the brackets. This is normal for some files.

**"LAPSE failed for this subtitle file: ...".** The error after it says what happened. The original subtitle is untouched. The LAPSE container log has more: `docker compose logs lapse`.

For everything about LAPSE itself, see the [main README](../README.md).
