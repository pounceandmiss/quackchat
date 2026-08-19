#include "RegistrationController.h"

#include <QCoreApplication>

#include "BackendBinding.h"
#include "TackyBackend.h"

namespace {
const auto kRegister = QStringLiteral("register");
const auto kToken = QStringLiteral("token");

// Unique per instance, and per process: on Android one tacky serves whoever
// connects to it, so a token only this process could have picked is what keeps
// two sign-ups apart.
QString nextToken() {
    static int counter = 0;
    return QStringLiteral("quack-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(++counter);
}
} // namespace

// In Role order, which is what lines the keys up with the roles.
RegistrationController::RegistrationController(QObject *parent)
    : MapListModel({"name", "type", "label", "required", "value", "values",
                    "options", "mediaSource", "hasMedia"},
                   parent),
      m_token(nextToken()) {}

RegistrationController::~RegistrationController() {
    // Not cancel(): the model must not reset itself on the way out. tacky is
    // holding a live connection for us either way, and only this tells it to
    // let go - an abandoned sign-up is otherwise a socket kept open for the
    // life of the backend.
    if (m_backend && m_backend->isRunning() && m_state != Idle)
        m_backend->notify(kRegister, QStringLiteral("cancel"),
                          QVariantMap{{kToken, m_token}});
}

void RegistrationController::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        // No re-read on the connected edge: a sign-up is the user's own doing
        // and starting one over behind them would be a second account.
        bindBackendWithoutReseed(this, m_backend);
        connect(m_backend, &TackyBackend::runningChanged, this,
                &RegistrationController::onRunningChanged);
    }
    emit backendChanged();
}

void RegistrationController::start(const QString &host, int port) {
    if (host.isEmpty())
        return;
    if (m_state != Idle)
        cancel(); // a session left on tacky's side would outlive this one
    m_host = host;
    m_port = port;
    emit hostChanged();
    m_entered.clear(); // another server asks its own questions
    connectSession();
}

void RegistrationController::retry() {
    if (m_host.isEmpty())
        return;
    // The answers stay in m_entered and go back into the form when it lands.
    connectSession();
}

// tacky replaces a session that already carries this token, so a retry needs no
// cancel first.
void RegistrationController::connectSession() {
    setError({});
    if (!m_backend || !m_backend->isRunning()) {
        setError(QStringLiteral("Not connected to the backend."));
        setState(Failed);
        return;
    }
    m_formToken = 0;
    m_mediaPending.clear();
    QVariantMap args{{kToken, m_token}, {QStringLiteral("host"), m_host}};
    if (m_port > 0)
        args.insert(QStringLiteral("port"), m_port);
    // Fire-and-forget: the outcome is a <Form> or an <Error>, never a reply.
    m_backend->notify(kRegister, QStringLiteral("connect"), args);
    setState(Connecting);
}

void RegistrationController::requestForm() {
    if (!m_backend)
        return;
    m_formToken = m_backend->request(kRegister, QStringLiteral("form"),
                                     QVariantMap{{kToken, m_token}});
}

void RegistrationController::requestMedia(const QString &var) {
    if (!m_backend || var.isEmpty())
        return;
    const int tok =
        m_backend->request(kRegister, QStringLiteral("media"),
                           QVariantMap{{kToken, m_token},
                                       {QStringLiteral("var"), var}});
    m_mediaPending.insert(tok, var);
}

void RegistrationController::submitForm() {
    if (!m_backend || m_state == Submitting || m_items.isEmpty())
        return;
    setError({});
    m_backend->notify(kRegister, QStringLiteral("submit"),
                      QVariantMap{{kToken, m_token},
                                  {QStringLiteral("values"), answers()}});
    setState(Submitting);
}

void RegistrationController::cancel() {
    if (m_backend && m_backend->isRunning() && m_state != Idle)
        m_backend->notify(kRegister, QStringLiteral("cancel"),
                          QVariantMap{{kToken, m_token}});
    m_formToken = 0;
    m_mediaPending.clear();
    m_entered.clear();
    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit completeChanged();
    }
    m_title.clear();
    m_instructions.clear();
    emit formChanged(); // hasForm with it: the rows went above
    emit instructionsChanged();
    setError({});
    setState(Idle);
}

void RegistrationController::handleEvent(const QString &module,
                                         const QString &name,
                                         const QVariant &args) {
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    if (module != kRegister)
        return;
    const QVariantMap a = args.toMap();
    if (a.value(kToken).toString() != m_token)
        return; // another instance's sign-up
    if (name == QLatin1String("Form")) {
        requestForm();
    } else if (name == QLatin1String("MediaReady")) {
        requestMedia(a.value(QStringLiteral("var")).toString());
    } else if (name == QLatin1String("Success")) {
        setState(Registered);
    } else if (name == QLatin1String("Error")) {
        setError(a.value(QStringLiteral("message")).toString());
        setState(Failed);
    }
}

void RegistrationController::handleResult(int token, const QVariant &data) {
    if (m_formToken != 0 && token == m_formToken) {
        m_formToken = 0;
        applyForm(data.toMap());
        setState(Ready);
        return;
    }
    const QString var = m_mediaPending.take(token);
    if (!var.isEmpty())
        applyMedia(var, data.toString());
}

void RegistrationController::handleError(int token, const QString &message) {
    if (m_formToken != 0 && token == m_formToken) {
        m_formToken = 0;
        setError(message);
        setState(Failed);
        return;
    }
    // A CAPTCHA whose bytes never came leaves the field showing that it has no
    // picture, which says as much as an error line would.
    m_mediaPending.remove(token);
}

void RegistrationController::onRunningChanged() {
    if (!m_backend || m_backend->isRunning())
        return;
    m_formToken = 0;
    m_mediaPending.clear();
    if (m_state == Connecting || m_state == Ready || m_state == Submitting) {
        // The connection tacky held for us went with it, so nothing is coming.
        setError(QStringLiteral("The backend stopped."));
        setState(Failed);
    }
}

// One row per visible field, in the order the server asked. Hidden fields are
// dropped: tacky submits from its own copy of the form and takes back only the
// vars named here, so they round-trip without being shown.
void RegistrationController::applyForm(const QVariantMap &form) {
    beginResetModel();
    m_items.clear();
    const QVariantList fields = form.value(QStringLiteral("fields")).toList();
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        const QString type = field.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("hidden"))
            continue;
        const QString var = field.value(QStringLiteral("var")).toString();

        // Values are always a list on the wire, so that a single- and a
        // multi-value field read the same. QML wants them apart.
        QStringList values;
        for (const QVariant &v : field.value(QStringLiteral("value")).toList())
            values.append(v.toString());
        // What the user typed beats what the server filled in: this is also
        // the form they were looking at before a retry re-fetched it.
        if (m_entered.contains(var)) {
            const QVariant entered = m_entered.value(var);
            values = entered.canConvert<QStringList>()
                         ? entered.toStringList()
                         : QStringList{entered.toString()};
        }

        QVariantMap row;
        row.insert(QStringLiteral("name"), var);
        row.insert(QStringLiteral("type"), type.isEmpty()
                                               ? QStringLiteral("text-single")
                                               : type);
        row.insert(QStringLiteral("label"),
                   field.value(QStringLiteral("label"), var));
        row.insert(QStringLiteral("required"),
                   field.value(QStringLiteral("required")).toBool());
        row.insert(QStringLiteral("value"), values.value(0));
        row.insert(QStringLiteral("values"), values);
        row.insert(QStringLiteral("options"),
                   field.value(QStringLiteral("options")).toList());
        const QVariantMap media = field.value(QStringLiteral("media")).toMap();
        row.insert(QStringLiteral("mediaSource"), QString());
        // The picture is a second round trip, so a field can be shown knowing
        // it has one before the bytes are here.
        row.insert(QStringLiteral("hasMedia"), !media.isEmpty());
        // Not a role: kept for the mime type a CAPTCHA's data: URL needs.
        row.insert(QStringLiteral("media"), media);
        m_items.append(row);
    }
    endResetModel();

    m_title = form.value(QStringLiteral("title")).toString();
    emit formChanged();
    const QString instructions =
        form.value(QStringLiteral("instructions")).toString();
    if (instructions != m_instructions) {
        m_instructions = instructions;
        emit instructionsChanged();
    }
    emit completeChanged();
}

void RegistrationController::applyMedia(const QString &var,
                                        const QString &base64) {
    const int row = rowOf(var);
    if (row < 0 || base64.isEmpty())
        return;
    // Straight into an Image.source: QML decodes a data: URL itself, so the
    // bytes need no image provider and no file on the way through.
    const QString type = m_items[row]
                             .value(QStringLiteral("media"))
                             .toMap()
                             .value(QStringLiteral("type"))
                             .toString();
    m_items[row].insert(QStringLiteral("mediaSource"),
                        QStringLiteral("data:%1;base64,%2")
                            .arg(type.isEmpty() ? QStringLiteral("image/png")
                                                : type,
                                 base64));
    emit dataChanged(index(row), index(row), {MediaSourceRole});
}

void RegistrationController::setValue(int row, const QVariant &value) {
    if (row < 0 || row >= m_items.size())
        return;
    const QString type = m_items[row].value(QStringLiteral("type")).toString();
    // By what it is, not what it could convert to: a QStringList converts to
    // a QString happily enough, and answers the wrong way round.
    const bool isList = value.typeId() == QMetaType::QStringList ||
                        value.typeId() == QMetaType::QVariantList;
    const QStringList values =
        isList ? value.toStringList() : QStringList{value.toString()};

    m_items[row].insert(QStringLiteral("value"), values.value(0));
    m_items[row].insert(QStringLiteral("values"), values);
    const QString var = m_items[row].value(QStringLiteral("name")).toString();
    if (isMulti(type))
        m_entered.insert(var, values);
    else
        m_entered.insert(var, values.value(0));
    emit dataChanged(index(row), index(row), {ValueRole, ValuesRole});
    emit completeChanged();
}

QString RegistrationController::valueFor(const QString &var) const {
    const int row = rowOf(var);
    return row < 0 ? QString()
                   : m_items.at(row).value(QStringLiteral("value")).toString();
}

// Every non-fixed field, empty ones included: leaving one out would keep
// whatever the server had put there, which is not what an emptied box means.
QVariantMap RegistrationController::answers() const {
    QVariantMap values;
    for (const QVariantMap &row : m_items) {
        const QString type = row.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("fixed"))
            continue;
        const QString var = row.value(QStringLiteral("name")).toString();
        if (var.isEmpty())
            continue;
        // A -multi field's answer goes over as a JSON array, which is the Tcl
        // list tacky's form code expects; everything else as one string.
        if (isMulti(type))
            values.insert(var, row.value(QStringLiteral("values")).toStringList());
        else
            values.insert(var, row.value(QStringLiteral("value")).toString());
    }
    return values;
}

bool RegistrationController::isComplete() const {
    if (m_items.isEmpty())
        return false;
    for (const QVariantMap &row : m_items) {
        if (!row.value(QStringLiteral("required")).toBool())
            continue;
        if (row.value(QStringLiteral("type")).toString() ==
            QLatin1String("fixed"))
            continue;
        if (row.value(QStringLiteral("value")).toString().isEmpty())
            return false;
    }
    return true;
}

// The server picked the field names, and only `username` says which account was
// made. Without one the caller has nothing to add locally and has to say so.
QString RegistrationController::registeredJid() const {
    if (m_state != Registered)
        return {};
    const QString user = valueFor(QStringLiteral("username"));
    if (user.isEmpty())
        return {};
    if (user.contains(QLatin1Char('@')))
        return user; // a server that asked for the whole JID
    return m_host.isEmpty() ? QString() : user + QLatin1Char('@') + m_host;
}

int RegistrationController::rowOf(const QString &var) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).value(QStringLiteral("name")).toString() == var)
            return i;
    }
    return -1;
}

bool RegistrationController::isMulti(const QString &type) {
    return type == QLatin1String("list-multi") ||
           type == QLatin1String("text-multi") ||
           type == QLatin1String("jid-multi");
}

void RegistrationController::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void RegistrationController::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}
