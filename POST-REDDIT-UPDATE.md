# Atualização para a thread — NÃO PUBLICADO

Poste como comentário na sua própria thread (fica no topo e notifica quem
comentou) ou como post novo, se preferir. O link vai no fim, e é só um.

Uma decisão sua antes: você me disse "está tudo 100%". Não escrevi assim, e o
motivo é prático — quem já testou vai testar de novo, e um bug encontrado dez
minutos depois de "tudo perfeito" custa mais do que ele vale. O texto abaixo diz
o que foi consertado com o defeito nomeado, que é mais forte e é verificável.

---

**Update: everything reported here is fixed, and the package lost 115 MB.**

Thanks for the reports — every one of them turned into a fix within a day.
v1.0.2 is up.

**Add-ons**

- Profiles that share the primary profile's add-ons came back with none. The web
  app has always had a `uses_primary_plugins` flag; the native port did not know
  it existed and asked for add-ons under the profile's own index.
- **Subtitles were never fetched at all on a synced account.** Add-ons pulled
  from the account were stored with the subtitles capability set to zero, three
  lines under a comment explaining that capabilities are assumed present. The
  subtitle search skips any add-on that does not declare subtitles, so it
  queried nothing, ever. Nobody had subtitles and nothing on screen said why.
- A disabled add-on was dropped when the list was read, so you could not see it
  or switch it back on from the TV — only from a phone. It is kept now and
  skipped only when queries run.
- There is a screen for this: Settings → Account → Add-ons. It shows what each
  one actually provides, read from its manifest rather than assumed, and OK
  toggles it. The toggle syncs, so switching one off on the TV switches it off
  everywhere.

**Languages**

- The interface language setting did nothing. The function that reads it had
  zero call sites — it stored a value nobody looked at. English is now the
  default and the setting works.
- Subtitle search had Portuguese and English nailed into the source and dropped
  every other language in silence. If you speak Spanish you opened the player
  and found nothing, with no indication that a filter existed. Preferences now
  come from your account, and **no preference means no filter** — showing
  everything is the honest answer to not knowing what you want.

**Trakt**

Linking it stored the token and changed nothing on screen until you relaunched,
because the catalogue is built once at startup. It rebuilds now, and a request
that arrives mid-sync waits instead of being dropped — which was the common
case, since you link Trakt while the startup sync is still running.

**Size**

313 MB → 198 MB. The 4K hero art was PNG with no alpha channel; the same art as
JPEG is 29 MB instead of 145 MB.

**Still not verified, same as before**

Only tested on a rooted C9. A non-rooted install needs LS2 access to
`com.webos.media` and I have not measured whether it is granted — if it is not,
expect the UI to run and the video plane to stay black. And this build is
webOS 4.x: its video path uses libAcbAPI, which LG removed in webOS 5, so on a
newer set nothing will play. For those, the web build is the one.

**If your TV is not webOS 4**

There is a sibling repo for that: the JavaScript fork has a webOS 4 release that
I have run on webOS 5, and preview builds for webOS 3 (C8, B7), which this
native port does not target. Both are unofficial and not affiliated with
NuvioMedia.

https://github.com/iqui27/nuvio-native-legacy/releases/tag/v1.0.2
