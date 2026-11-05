// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class KEncoderWindow final : public QWidget
{
public:
    KEncoderWindow()
    {
        setWindowTitle(QStringLiteral("KEncoder | HP OMEN Encoder"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("kencoder"),
                                       QIcon(QStringLiteral(":/icons/kencoder.png"))));
        setMinimumWidth(440);

        // The content keeps the normal KDE spacing. Only the bottom status
        // field spans the full window width and touches the bottom edge.
        auto *outerLayout = new QVBoxLayout(this);
        outerLayout->setContentsMargins(0, 0, 0, 0);
        outerLayout->setSpacing(0);
        auto *content = new QWidget(this);
        auto *layout = new QVBoxLayout(content);
        outerLayout->addWidget(content, 1);

        auto *controls = new QWidget(content);
        auto *form = new QFormLayout(controls);
        form->setContentsMargins(0, 0, 0, 0);

        animation_ = new QComboBox(controls);
        animation_->setObjectName(QStringLiteral("animationMode"));
        animation_->addItem(QStringLiteral("Statické"), QStringLiteral("static"));
        animation_->addItem(QStringLiteral("Dýchání"), QStringLiteral("breathe"));
        animation_->addItem(QStringLiteral("Vlna zleva"), QStringLiteral("left_wave"));
        animation_->addItem(QStringLiteral("Vlna zprava"), QStringLiteral("right_wave"));
        animation_->addItem(QStringLiteral("Reakce na stisk"), QStringLiteral("ripple"));
        form->addRow(QStringLiteral("Animace:"), animation_);

        auto *brightnessRow = new QWidget(controls);
        auto *brightnessLayout = new QHBoxLayout(brightnessRow);
        brightnessLayout->setContentsMargins(0, 0, 0, 0);
        brightness_ = new QSlider(Qt::Horizontal, brightnessRow);
        brightness_->setObjectName(QStringLiteral("brightnessSlider"));
        brightness_->setRange(0, 3); // Four hardware brightness levels.
        brightness_->setPageStep(1);
        brightness_->setTickInterval(1);
        brightness_->setTickPosition(QSlider::TicksBelow);
        brightness_->setValue(1); // GUI default, not a hardware readback.
        brightnessLayout->addWidget(brightness_);
        brightnessValue_ = new QLabel(QString::number(brightness_->value()), brightnessRow);
        brightnessValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        brightnessValue_->setMinimumWidth(24);
        brightnessLayout->addWidget(brightnessValue_);
        form->addRow(QStringLiteral("Jas (0 až 3):"), brightnessRow);

        auto *speedRow = new QWidget(controls);
        auto *speedLayout = new QHBoxLayout(speedRow);
        speedLayout->setContentsMargins(0, 0, 0, 0);
        speed_ = new QSlider(Qt::Horizontal, speedRow);
        speed_->setObjectName(QStringLiteral("animationSpeed"));
        speed_->setRange(0, 2); // Slow / medium / fast.
        speed_->setPageStep(1);
        speed_->setTickInterval(1);
        speed_->setTickPosition(QSlider::TicksBelow);
        speed_->setValue(1);
        speedLayout->addWidget(speed_);
        speedValue_ = new QLabel(speedLabel(speed_->value()), speedRow);
        speedValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        speedValue_->setMinimumWidth(70);
        speedLayout->addWidget(speedValue_);
        speedCaption_ = new QLabel(QStringLiteral("Rychlost:"), controls);
        form->addRow(speedCaption_, speedRow);
        layout->addWidget(controls);

        closeToTray_ = new QCheckBox(QStringLiteral("Zavřít do systémového panelu"), content);
        closeToTray_->setObjectName(QStringLiteral("closeToTray"));
        closeToTray_->setChecked(QSettings().value(QStringLiteral("ui/closeToTray"), false).toBool());
        layout->addWidget(closeToTray_);

        auto *buttonRow = new QHBoxLayout();
        auto *applyButton = makeButton(QStringLiteral("Nastavit"), buttonRow);
        auto *offButton = makeButton(QStringLiteral("Vypnout"), buttonRow);
        layout->addLayout(buttonRow);

        layout->addSpacing(12);
        healthTitle_ = new QLabel(QStringLiteral("Stav"), content);
        healthTitle_->setObjectName(QStringLiteral("runtimeStatusTitle"));
        QFont titleFont = healthTitle_->font();
        titleFont.setBold(true);
        healthTitle_->setFont(titleFont);
        layout->addWidget(healthTitle_);
        layout->addSpacing(4);

        auto *healthRow = new QHBoxLayout();
        healthLed_ = new QLabel(content);
        healthLed_->setObjectName(QStringLiteral("healthLed"));
        healthLed_->setFixedSize(12, 12);
        healthText_ = new QLabel(content);
        healthText_->setObjectName(QStringLiteral("healthText"));
        healthText_->setWordWrap(true);
        healthText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        healthText_->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // Center the LED on the FIRST text line, not on the whole wrapped label.
        // The top spacer follows the selected KDE font's line metrics.
        auto *ledColumn = new QVBoxLayout();
        ledColumn->setContentsMargins(0, 0, 0, 0);
        ledColumn->setSpacing(0);
        ledColumn->addSpacing(qMax(0, (healthText_->fontMetrics().lineSpacing() -
                                    healthLed_->height()) / 2));
        ledColumn->addWidget(healthLed_, 0, Qt::AlignHCenter);
        ledColumn->addStretch();
        healthRow->addLayout(ledColumn);
        healthRow->addWidget(healthText_, 1);
        layout->addLayout(healthRow);

        // Keep all the free space inside the content area, above the status field.
        layout->addStretch(1);
        deviceText_ = QStringLiteral("HP OMEN Encoder (03f0:3341)");
        statusField_ = new QLineEdit(this);
        statusField_->setObjectName(QStringLiteral("deviceStatusField"));
        statusField_->setAccessibleName(QStringLiteral("Stav zařízení a zprávy"));
        statusField_->setReadOnly(true);
        statusField_->setFrame(true); // Native KDE sunken text field.
        statusField_->setText(deviceText_);
        statusField_->setToolTip(deviceText_);
        outerLayout->addWidget(statusField_);

        statusReset_.setSingleShot(true);
        connect(&statusReset_, &QTimer::timeout, this, [this] { restoreDeviceStatus(); });

        trayIcon_ = new QSystemTrayIcon(windowIcon(), this);
        trayIcon_->setToolTip(QStringLiteral("KEncoder"));
        auto *trayMenu = new QMenu(this);
        trayToggleAction_ = trayMenu->addAction(QStringLiteral("Skrýt"), this,
                                              [this] { toggleFromTray(); });
        trayMenu->addSeparator();
        trayMenu->addAction(QStringLiteral("Ukončit"), qApp, [] { QCoreApplication::quit(); });
        trayIcon_->setContextMenu(trayMenu);
        connect(trayIcon_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
            // A double-click may also generate a Trigger: toggle only once.
            if (reason == QSystemTrayIcon::Trigger)
                toggleFromTray();
        });

        connect(animation_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this] { updateSpeedEnabled(); });
        connect(brightness_, &QSlider::valueChanged, brightnessValue_,
                [this](int level) { brightnessValue_->setText(QString::number(level)); });
        connect(speed_, &QSlider::valueChanged, speedValue_,
                [this](int level) { speedValue_->setText(speedLabel(level)); });
        connect(closeToTray_, &QCheckBox::toggled, this, [this](bool checked) {
            QSettings().setValue(QStringLiteral("ui/closeToTray"), checked);
            updateTrayIcon();
            if (checked && !QSystemTrayIcon::isSystemTrayAvailable())
                showStatus(QStringLiteral("Systémový panel není dostupný; "
                                          "zavření okna aplikaci ukončí."), 8000);
        });
        updateSpeedEnabled();
        updateTrayIcon();

        connect(applyButton, &QPushButton::clicked, this, [this] {
            QStringList arguments{QStringLiteral("-b"), QString::number(brightness_->value())};
            if (animation_->currentData().toString() == QLatin1String("static")) {
                arguments << QStringLiteral("-s");
                run(arguments, QStringLiteral("Statické podsvícení nastaveno"));
            } else {
                arguments << QStringLiteral("-a") << animation_->currentData().toString()
                          << QStringLiteral("-v") << speedArgument(speed_->value());
                run(arguments, QStringLiteral("Animace nastavena"));
            }
        });
        connect(offButton, &QPushButton::clicked, this, [this] {
            run({QStringLiteral("-o")}, QStringLiteral("Podsvícení vypnuto"));
        });

        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this, [this] {
            timedOut_ = true;
            process_.kill();
        });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            timeout_.stop();
            if (timedOut_)
                return; // Finished handler provides the more precise timeout reason.
            lastCommandError_ = error == QProcess::FailedToStart
                ? QStringLiteral("Nelze spustit encoder. Zkontrolujte instalaci.")
                : QStringLiteral("Chyba programu encoder: %1").arg(process_.errorString());
            showStatus(lastCommandError_, 8000);
            refreshHealth();
            setBusy(false);
        });
        connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            timeout_.stop();
            if (timedOut_) {
                lastCommandError_ = QStringLiteral("Klávesnice neodpověděla do 8 sekund. "
                                                   "Proces byl ukončen.");
            } else {
                const QString stdoutText = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
                const QString stderrText = QString::fromUtf8(process_.readAllStandardError()).trimmed();
                if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
                    !stdoutText.contains(QStringLiteral("Command failed"))) {
                    lastCommandError_.clear();
                    showStatus(QStringLiteral("Potvrzeno: %1").arg(successMessage_), 5000);
                } else {
                    const QString detail = !stderrText.isEmpty() ? stderrText : stdoutText;
                    lastCommandError_ = QStringLiteral("Příkaz selhal (kód %1). %2")
                        .arg(exitCode).arg(detail);
                }
            }
            if (!lastCommandError_.isEmpty())
                showStatus(lastCommandError_, 8000);
            refreshHealth();
            setBusy(false);
        });

        const bool selfTesting = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));
        if (!selfTesting) {
            refreshHealth();
            healthPoll_.setInterval(5000);
            connect(&healthPoll_, &QTimer::timeout, this, [this] { refreshHealth(); });
            healthPoll_.start();
        } else {
            setHealth(HealthLevel::Warning, QStringLiteral("Diagnostika vynechána při self-testu."));
        }
    }

    bool selfTest()
    {
        // A failing headless CI test must identify the failed condition.
        auto require = [](bool condition, const char *description) {
            if (!condition)
                qCritical().noquote() << "KEncoder self-test FAIL:" << description;
            return condition;
        };
        if (!require(!windowTitle().isEmpty(), "window title") ||
            !require(!windowIcon().isNull(), "embedded window icon") ||
            !require(buttons_.size() == 2, "button count") ||
            !require(animation_->currentData().toString() == QLatin1String("static"),
                     "default animation") ||
            !require(brightness_->value() == 1, "default brightness") ||
            !require(speed_->value() == 1, "default speed") ||
            !require(!speed_->isEnabled() && !speedValue_->isEnabled(),
                     "static mode must disable speed") ||
            !require(healthLed_ && healthText_ && healthTitle_ && statusField_,
                     "status widgets") ||
            !require(statusField_ && statusField_->isReadOnly(), "read-only status field") ||
            !require(trayToggleAction_ != nullptr, "system tray action") ||
            !require(trayToggleAction_ &&
                     trayToggleAction_->text() == QLatin1String("Zobrazit"),
                     "initial tray action") ||
            !require(healthTitle_ && healthTitle_->text() == QLatin1String("Stav"),
                     "status title") ||
            !require(statusField_ &&
                     statusField_->text() == QLatin1String("HP OMEN Encoder (03f0:3341)"),
                     "initial device status"))
            return false;
        // Status field is the last item, outside the padded content layout.
        auto *outer = qobject_cast<QVBoxLayout *>(layout());
        if (!require(outer != nullptr, "outer layout exists"))
            return false;
        if (!require(outer->contentsMargins().isNull(), "zero outer margins") ||
            !require(outer->count() == 2, "two outer layout elements") ||
            !require(outer->itemAt(1)->widget() == statusField_,
                     "status field at bottom edge"))
            return false;
        if (!require(classifyHealth(true, true, true, true, true, true) == HealthLevel::Good,
                     "all healthy state") ||
            !require(classifyHealth(true, true, false, false, false, true) == HealthLevel::Warning,
                     "missing udev rule state") ||
            !require(classifyHealth(true, true, true, false, true, true) == HealthLevel::Warning,
                     "udev rule not applied") ||
            !require(classifyHealth(false, true, true, true, true, true) == HealthLevel::Error,
                     "missing CLI state") ||
            !require(classifyHealth(true, false, true, true, true, true) == HealthLevel::Error,
                     "missing device state") ||
            !require(classifyHealth(true, true, true, true, false, true) == HealthLevel::Error,
                     "inaccessible device state") ||
            !require(classifyHealth(true, true, true, true, true, false) == HealthLevel::Error,
                     "command error state"))
            return false;
        animation_->setCurrentIndex(1);
        if (!require(speed_->isEnabled() && speedValue_->isEnabled() &&
                     speedCaption_->isEnabled(), "animation must enable speed"))
            return false;
        animation_->setCurrentIndex(0);
        if (!require(!speed_->isEnabled() && !speedValue_->isEnabled() &&
                     !speedCaption_->isEnabled(), "static mode must disable speed"))
            return false;
        showStatus(QStringLiteral("Test zprávy"), 5000);
        if (!require(statusField_->text() == QStringLiteral("Test zprávy"),
                     "temporary status text"))
            return false;
        if (!require(statusReset_.isActive(), "temporary status timer active"))
            return false;
        statusReset_.stop();
        restoreDeviceStatus();
        return require(statusField_->text() == deviceText_, "restore device status");
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        // Never hide the only window unless a system tray is available.
        if (closeToTray_->isChecked() && QSystemTrayIcon::isSystemTrayAvailable()) {
            if (!trayIcon_->isVisible())
                trayIcon_->show();
            hide();
            updateTrayActionText();
            event->ignore();
            return;
        }
        QWidget::closeEvent(event);
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (trayToggleAction_)
            updateTrayActionText();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        if (trayToggleAction_)
            updateTrayActionText();
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange && trayToggleAction_)
            updateTrayActionText();
    }

private:
    enum class HealthLevel { Good, Warning, Error };

    static HealthLevel classifyHealth(bool hasCli, bool hasDevice, bool hasRule,
                                      bool hasTag, bool canAccess, bool commandOk)
    {
        if (!hasCli || !hasDevice || !commandOk)
            return HealthLevel::Error;
        if (!hasRule || !hasTag)
            return HealthLevel::Warning;
        if (!canAccess)
            return HealthLevel::Error;
        return HealthLevel::Good;
    }

    static QString speedLabel(int index)
    {
        switch (index) {
        case 0: return QStringLiteral("Pomalá");
        case 2: return QStringLiteral("Rychlá");
        default: return QStringLiteral("Střední");
        }
    }

    static QString speedArgument(int index)
    {
        switch (index) {
        case 0: return QStringLiteral("slow");
        case 2: return QStringLiteral("fast");
        default: return QStringLiteral("medium");
        }
    }

    void updateSpeedEnabled()
    {
        const bool animated = animation_->currentData().toString() != QLatin1String("static");
        speed_->setEnabled(animated);
        speedValue_->setEnabled(animated);
        speedCaption_->setEnabled(animated);
    }

    void updateTrayIcon()
    {
        trayIcon_->setVisible(closeToTray_->isChecked() && QSystemTrayIcon::isSystemTrayAvailable());
        updateTrayActionText();
    }

    void updateTrayActionText()
    {
        trayToggleAction_->setText(isVisible() && !isMinimized()
            ? QStringLiteral("Skrýt") : QStringLiteral("Zobrazit"));
    }

    void toggleFromTray()
    {
        if (isVisible() && !isMinimized()) {
            hide();
        } else {
            showNormal();
            raise();
            activateWindow();
        }
        updateTrayActionText();
    }

    QPushButton *makeButton(const QString &label, QHBoxLayout *layout)
    {
        auto *button = new QPushButton(label, this);
        layout->addWidget(button);
        buttons_.append(button);
        return button;
    }

    void setBusy(bool busy)
    {
        for (QPushButton *button : buttons_)
            button->setEnabled(!busy);
    }

    static QString vendorDevice()
    {
        // Only the vendor HID interface may receive lighting commands; other
        // interfaces on the keyboard carry keyboard and multimedia input.
        const QDir hidraw(QStringLiteral("/sys/class/hidraw"));
        const QStringList entries = hidraw.entryList({QStringLiteral("hidraw*")},
                                                     QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            QFile uevent(hidraw.filePath(entry + QStringLiteral("/device/uevent")));
            if (!uevent.open(QIODevice::ReadOnly) ||
                !uevent.readAll().toUpper().contains("HID_ID=0003:000003F0:00003341"))
                continue;
            QFile descriptor(hidraw.filePath(entry + QStringLiteral("/device/report_descriptor")));
            if (!descriptor.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = descriptor.readAll();
            if (bytes.size() == 30 && bytes.startsWith(QByteArray::fromHex("0613ff0901a101")))
                return QStringLiteral("/dev/") + entry;
        }
        return {};
    }

    static bool hasEffectiveUdevRule(const QString &name, const QList<QByteArray> &tokens)
    {
        // Respect udev file precedence: /etc overrides /run, which overrides /usr.
        const QStringList dirs{
            QStringLiteral("/etc/udev/rules.d/"),
            QStringLiteral("/run/udev/rules.d/"),
            QStringLiteral("/usr/local/lib/udev/rules.d/"),
            QStringLiteral("/usr/lib/udev/rules.d/"),
            QStringLiteral("/lib/udev/rules.d/")
        };
        for (const QString &dir : dirs) {
            const QString path = dir + name;
            const QFileInfo info(path);
            if (!info.exists() && !info.isSymLink())
                continue;
            QFile rule(path);
            if (!rule.open(QIODevice::ReadOnly))
                return false;
            const QByteArray content = rule.readAll();
            for (const QByteArray &token : tokens) {
                if (!content.contains(token))
                    return false;
            }
            return true;
        }
        return false;
    }

    static bool hasManagedUdevRules()
    {
        // Tag before 73-seat-late.rules; repair MODE after upstream 97- rule.
        return hasEffectiveUdevRule(QStringLiteral("70-encoder-uaccess.rules"),
                                    {"03f0", "3341", "uaccess"}) &&
               hasEffectiveUdevRule(QStringLiteral("99-encoder.rules"),
                                    {"03f0", "3341", "0660"});
    }

    static bool hasLiveUaccessTag(const QString &node, bool *querySucceeded)
    {
        *querySucceeded = false;
        const QString command = QStandardPaths::findExecutable(QStringLiteral("udevadm"));
        if (command.isEmpty())
            return false;

        // Query the current udev database; do not reload rules or send USB data.
        QProcess udev;
        udev.start(command, {QStringLiteral("info"), QStringLiteral("--query=property"),
                             QStringLiteral("--name=") + node});
        if (!udev.waitForStarted(1000))
            return false;
        if (!udev.waitForFinished(1500)) {
            udev.kill();
            udev.waitForFinished(250);
            return false;
        }
        if (udev.exitStatus() != QProcess::NormalExit || udev.exitCode() != 0)
            return false;
        QByteArray tags;
        QByteArray currentTags;
        const QList<QByteArray> lines = udev.readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("CURRENT_TAGS="))
                currentTags = line.mid(sizeof("CURRENT_TAGS=") - 1);
            else if (line.startsWith("TAGS="))
                tags = line.mid(sizeof("TAGS=") - 1);
        }
        *querySucceeded = true;
        const QByteArray effectiveTags = currentTags.isEmpty() ? tags : currentTags;
        return effectiveTags.split(':').contains(QByteArrayLiteral("uaccess"));
    }

    // Transient command result in the same read-only field as the HID device.
    // Health polling must not replace the message before the timer expires.
    void showStatus(const QString &message, int durationMs)
    {
        statusReset_.stop();
        statusField_->setText(message);
        statusField_->setToolTip(message);
        if (durationMs > 0)
            statusReset_.start(durationMs);
    }

    void restoreDeviceStatus()
    {
        statusField_->setText(deviceText_);
        statusField_->setToolTip(deviceText_);
    }

    void setHealth(HealthLevel level, const QString &message)
    {
        const char *color = level == HealthLevel::Good ? "#209c60"
                          : level == HealthLevel::Warning ? "#d68b16" : "#d63c43";
        const QString name = level == HealthLevel::Good ? QStringLiteral("V pořádku")
                             : level == HealthLevel::Warning ? QStringLiteral("Upozornění")
                             : QStringLiteral("Problém");
        healthLed_->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 6px;")
                                  .arg(QString::fromLatin1(color)));
        healthLed_->setAccessibleName(name);
        healthLed_->setToolTip(name + QStringLiteral(": ") + message);
        healthText_->setText(message);
        healthText_->setToolTip(message);
    }

    void refreshHealth()
    {
        const bool hasCli = !QStandardPaths::findExecutable(QStringLiteral("encoder")).isEmpty();
        const QString node = vendorDevice();
        const bool hasDevice = !node.isEmpty();
        deviceText_ = hasDevice
            ? QStringLiteral("HP OMEN Encoder (03f0:3341) | ") + node
            : QStringLiteral("HP OMEN Encoder (03f0:3341) | nepřipojeno");
        if (!statusReset_.isActive())
            restoreDeviceStatus();

        const bool hasRule = hasManagedUdevRules();
        bool udevQueryOk = false;
        const bool hasTag = hasDevice && hasRule && hasLiveUaccessTag(node, &udevQueryOk);
        bool accessible = false;
        if (hasDevice && hasTag) {
            // O_RDWR is required by the CLI. No read/write/feature report is sent.
            QFile device(node);
            accessible = device.open(QIODevice::ReadWrite | QIODevice::Unbuffered);
            device.close();
        }
        const HealthLevel level = classifyHealth(hasCli, hasDevice, hasRule, hasTag,
                                                  accessible, lastCommandError_.isEmpty());
        QString message;
        if (!lastCommandError_.isEmpty())
            message = lastCommandError_;
        else if (!hasCli)
            message = QStringLiteral("Chybí CLI encoder. Nainstalujte balíček encoder.");
        else if (!hasDevice)
            message = QStringLiteral("Klávesnice HP OMEN Encoder není připojena.");
        else if (!hasRule)
            message = QStringLiteral("Chybí udev pravidlo 70-encoder-uaccess.rules nebo 99-encoder.rules.");
        else if (!hasTag)
            message = udevQueryOk
                ? QStringLiteral("udev pravidlo není aplikováno. Znovu připojte klávesnici.")
                : QStringLiteral("Nelze ověřit stav udev. Zkontrolujte udevadm.");
        else if (!accessible)
            message = QStringLiteral("Nelze otevřít %1 pro čtení a zápis. Zkontrolujte oprávnění.").arg(node);
        else
            message = QStringLiteral("Připraveno: CLI, udev a přístup ke klávesnici jsou funkční.");
        setHealth(level, message);
    }

    void run(const QStringList &arguments, const QString &successMessage)
    {
        if (process_.state() != QProcess::NotRunning)
            return;
        if (QStandardPaths::findExecutable(QStringLiteral("encoder")).isEmpty()) {
            lastCommandError_ = QStringLiteral("Chybí CLI encoder. Nainstalujte balíček encoder.");
            showStatus(lastCommandError_, 8000);
            refreshHealth();
            return;
        }
        // Clear a previous command failure only on a successful command, not on retry.
        successMessage_ = successMessage;
        timedOut_ = false;
        showStatus(QStringLiteral("Odesílám nastavení do klávesnice…"), 10000);
        setBusy(true);
        process_.start(QStringLiteral("encoder"), arguments);
        timeout_.start(8000);
    }

    QSlider *brightness_ = nullptr;
    QSlider *speed_ = nullptr;
    QComboBox *animation_ = nullptr;
    QCheckBox *closeToTray_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QAction *trayToggleAction_ = nullptr;
    QLabel *brightnessValue_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QLabel *speedCaption_ = nullptr;
    QLabel *healthTitle_ = nullptr;
    QLabel *healthLed_ = nullptr;
    QLabel *healthText_ = nullptr;
    QLineEdit *statusField_ = nullptr;
    QString deviceText_;
    QTimer statusReset_;
    QList<QPushButton *> buttons_;
    QProcess process_;
    QTimer timeout_;
    QTimer healthPoll_;
    bool timedOut_ = false;
    QString successMessage_;
    QString lastCommandError_;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kencoder"));
    app.setOrganizationName(QStringLiteral("Pitriss"));
    KEncoderWindow window;
    if (QCoreApplication::arguments().contains(QStringLiteral("--self-test"))) {
        // QTimer is tested with the real Qt event loop running. The headless
        // self-test never shows a window or accesses the HID device.
        QTimer::singleShot(0, &window, [&window] {
            QCoreApplication::exit(window.selfTest() ? 0 : 1);
        });
        return app.exec();
    }
    window.show();
    return app.exec();
}
