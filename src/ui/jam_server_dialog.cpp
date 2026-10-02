#include "ui/jam_server_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Ui {

namespace {
constexpr int kDialogWidth = 420;
}  // namespace

JamServerDialog::JamServerDialog(
    const QString& server,
    bool waveFeedback,
    bool shareAudio,
    const QString& defaultServer,
    QWidget* parent
)
    : QDialog(parent)
    , fallback(defaultServer) {
    setWindowTitle(tr("Jam server"));
    setMinimumWidth(kDialogWidth);
    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Server address")));
    serverField = new QLineEdit(server == defaultServer ? QString() : server);
    serverField->setPlaceholderText(defaultServer);
    layout->addWidget(serverField);
    serverProblem =
        new QLabel(tr("An address like %1 is needed").arg(QStringLiteral("https://jam.example.org"))
        );
    serverProblem->setVisible(false);
    layout->addWidget(serverProblem);

    feedbackBox = new QCheckBox(tr("Teach the jam vibe"));
    feedbackBox->setChecked(waveFeedback);
    layout->addWidget(feedbackBox);
    auto* note = new QLabel(tr(
        "When the guests' queue is empty, the jam vibe plays. On: it learns from the skips during "
        "the jam, and only there. Off: no feedback at all."
    ));
    note->setWordWrap(true);
    layout->addWidget(note);

    shareBox = new QCheckBox(tr("Guests may listen"));
    shareBox->setChecked(shareAudio);
    layout->addWidget(shareBox);
    auto* shareNote = new QLabel(tr(
        "The jam page gets a Listen button: the guests' browsers play the files you play, roughly "
        "in time with you. They are your subscription's files: Yandex Music licenses them for "
        "your own listening."
    ));
    shareNote->setWordWrap(true);
    layout->addWidget(shareNote);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    saveButton = buttons->button(QDialogButtonBox::Save);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    saveButton->setText(tr("Save"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(serverField, &QLineEdit::textChanged, this, &JamServerDialog::check);
    check();
}

QString JamServerDialog::server() const {
    const QString typed = serverField->text().trimmed();
    return typed.isEmpty() ? fallback : typed;
}

bool JamServerDialog::shareAudio() const {
    return shareBox->isChecked();
}

bool JamServerDialog::waveFeedback() const {
    return feedbackBox->isChecked();
}

bool JamServerDialog::IsServerAddress(const QString& text) {
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    return url.isValid()
        && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        && !url.host().isEmpty();
}

void JamServerDialog::check() {
    const bool valid = IsServerAddress(server());
    serverProblem->setVisible(!valid);
    saveButton->setEnabled(valid);
}

}  // namespace Ui
