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
    const QString& defaultServer,
    QWidget* parent
)
    : QDialog(parent)
    , fallback(defaultServer) {
    setWindowTitle(QStringLiteral("Сервер джема"));
    setMinimumWidth(kDialogWidth);
    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(QStringLiteral("Адрес сервера")));
    serverField = new QLineEdit(server == defaultServer ? QString() : server);
    serverField->setPlaceholderText(defaultServer);
    layout->addWidget(serverField);
    serverProblem = new QLabel(QStringLiteral("Нужен адрес вида https://jam.example.org"));
    serverProblem->setVisible(false);
    layout->addWidget(serverProblem);

    feedbackBox = new QCheckBox(QStringLiteral("Учить волну джема"));
    feedbackBox->setChecked(waveFeedback);
    layout->addWidget(feedbackBox);
    auto* note = new QLabel(QStringLiteral(
        "Когда очередь гостей пуста, играет волна джема. Включено: она учится на пропусках во "
        "время джема и только там. Выключено: никакого фидбека."
    ));
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    saveButton = buttons->button(QDialogButtonBox::Save);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    saveButton->setText(QStringLiteral("Сохранить"));
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
