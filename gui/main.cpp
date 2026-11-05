// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class KEncoderWindow final : public QWidget
{
public:
    KEncoderWindow()
    {
        setWindowTitle(QStringLiteral("KEncoder | HP OMEN Encoder"));
        setMinimumWidth(440);

        auto *layout = new QVBoxLayout(this);
        auto *header = new QLabel(QStringLiteral("Ovládání podsvícení klávesnice"), this);
        QFont titleFont = header->font();
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleFont.setBold(true);
        header->setFont(titleFont);
        layout->addWidget(header);

        auto *subtitle = new QLabel(QStringLiteral("HP OMEN Encoder (USB 03f0:3341)"), this);
        layout->addWidget(subtitle);

        auto *group = new QGroupBox(QStringLiteral("Nastavení podsvícení"), this);
        auto *form = new QFormLayout(group);

        brightness_ = new QComboBox(group);
        for (int level = 0; level <= 3; ++level)
            brightness_->addItem(QString::number(level), level);
        brightness_->setCurrentIndex(3);
        form->addRow(QStringLiteral("Jas (0 až 3):"), brightness_);

        animation_ = new QComboBox(group);
        animation_->addItem(QStringLiteral("Dýchání"), QStringLiteral("breathe"));
        animation_->addItem(QStringLiteral("Vlna zleva"), QStringLiteral("left_wave"));
        animation_->addItem(QStringLiteral("Vlna zprava"), QStringLiteral("right_wave"));
        animation_->addItem(QStringLiteral("Reakce na stisk"), QStringLiteral("ripple"));
        form->addRow(QStringLiteral("Animace:"), animation_);

        speed_ = new QComboBox(group);
        speed_->addItem(QStringLiteral("Pomalá"), QStringLiteral("slow"));
        speed_->addItem(QStringLiteral("Střední"), QStringLiteral("medium"));
        speed_->addItem(QStringLiteral("Rychlá"), QStringLiteral("fast"));
        speed_->setCurrentIndex(1);
        form->addRow(QStringLiteral("Rychlost:"), speed_);
        layout->addWidget(group);

        auto *firstRow = new QHBoxLayout();
        auto *brightnessButton = makeButton(QStringLiteral("Nastavit jas"), firstRow);
        auto *staticButton = makeButton(QStringLiteral("Statické podsvícení"), firstRow);
        layout->addLayout(firstRow);

        auto *secondRow = new QHBoxLayout();
        auto *animateButton = makeButton(QStringLiteral("Spustit animaci"), secondRow);
        auto *offButton = makeButton(QStringLiteral("Vypnout"), secondRow);
        layout->addLayout(secondRow);

        status_ = new QLabel(this);
        status_->setWordWrap(true);
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(status_);

        const bool hasEncoder = !QStandardPaths::findExecutable(QStringLiteral("encoder")).isEmpty();
        status_->setText(hasEncoder
            ? QStringLiteral("Připraveno. Příkazy se provádějí bez oprávnění root.")
            : QStringLiteral("Program encoder není v PATH. Nainstalujte balíček encoder."));

        connect(brightnessButton, &QPushButton::clicked, this, [this] {
            run({QStringLiteral("-b"), brightnessValue()}, QStringLiteral("Jas nastaven"));
        });
        connect(staticButton, &QPushButton::clicked, this, [this] {
            run({QStringLiteral("-b"), brightnessValue(), QStringLiteral("-s")},
                QStringLiteral("Statické podsvícení zapnuto"));
        });
        connect(animateButton, &QPushButton::clicked, this, [this] {
            run({QStringLiteral("-b"), brightnessValue(), QStringLiteral("-a"),
                 animation_->currentData().toString(), QStringLiteral("-v"),
                 speed_->currentData().toString()},
                QStringLiteral("Animace nastavena"));
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
            if (error == QProcess::FailedToStart)
                status_->setText(QStringLiteral("Nelze spustit encoder. Zkontrolujte instalaci."));
            else
                status_->setText(QStringLiteral("Chyba programu encoder: %1").arg(process_.errorString()));
            setBusy(false);
        });
        connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            timeout_.stop();
            if (timedOut_) {
                status_->setText(QStringLiteral("Klávesnice neodpověděla do 8 sekund. Proces byl ukončen."));
                setBusy(false);
                return;
            }
            const QString stdoutText = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
            const QString stderrText = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
                !stdoutText.contains(QStringLiteral("Command failed"))) {
                status_->setText(successMessage_);
            } else {
                const QString detail = !stderrText.isEmpty() ? stderrText : stdoutText;
                status_->setText(QStringLiteral("Příkaz selhal (kód %1). %2")
                    .arg(exitCode).arg(detail));
            }
            setBusy(false);
        });
    }

private:
    QPushButton *makeButton(const QString &label, QHBoxLayout *layout)
    {
        auto *button = new QPushButton(label, this);
        layout->addWidget(button);
        buttons_.append(button);
        return button;
    }

    QString brightnessValue() const
    {
        return QString::number(brightness_->currentData().toInt());
    }

    void setBusy(bool busy)
    {
        for (QPushButton *button : buttons_)
            button->setEnabled(!busy);
    }

    void run(const QStringList &arguments, const QString &successMessage)
    {
        if (process_.state() != QProcess::NotRunning)
            return;
        successMessage_ = successMessage;
        timedOut_ = false;
        status_->setText(QStringLiteral("Odesílám nastavení do klávesnice…"));
        setBusy(true);
        process_.start(QStringLiteral("encoder"), arguments);
        timeout_.start(8000);
    }

    QComboBox *brightness_ = nullptr;
    QComboBox *animation_ = nullptr;
    QComboBox *speed_ = nullptr;
    QLabel *status_ = nullptr;
    QList<QPushButton *> buttons_;
    QProcess process_;
    QTimer timeout_;
    bool timedOut_ = false;
    QString successMessage_;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kencoder"));
    app.setOrganizationName(QStringLiteral("Pitriss"));
    KEncoderWindow window;
    if (QCoreApplication::arguments().contains(QStringLiteral("--self-test")))
        return window.windowTitle().isEmpty() ? 1 : 0;
    window.show();
    return app.exec();
}
