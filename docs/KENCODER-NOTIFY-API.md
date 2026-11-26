# KEncoder Notify: Custom API

Alternativní backend `custom-api` přijímá externí události prostřednictvím uživatelské D-Bus relace.

## D-Bus rozhraní

```text
Service:   org.kencoder.Notify1
Object:    /org/kencoder/Notify1
Interface: org.kencoder.Notify1
Method:    SubmitNotification(sss) -> b
```

Argumenty:

- `app`: identifikátor aplikace, maximálně 256 znaků
- `title`: nadpis události, maximálně 1024 znaků
- `body`: text události, maximálně 8192 znaků

Návratová hodnota `true` potvrzuje zařazení události, nikoliv dokončení světelného efektu.

## CLI

```bash
kencoder-notify --submit "backup" "Dokončeno" "Záloha úspěšná"
```

## Přímé D-Bus volání

```bash
dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.SubmitNotification string:backup string:Dokonceno string:OK
```

Událost se vyhodnocuje existujícími pravidly, cooldowny, mute a session policy. Přijímá se pouze při aktivním backendu `custom-api`.

Prázdné události, nepovolené události a události bez odpovídajícího pravidla mohou být odmítnuty.
