// The Milkdrop preset collection: the built-in selection (Qt resources) plus
// the user's own .milk files, in a stable order.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace qiyaa {

class MilkdropPresets {
public:
    struct Preset {
        QString name;  // file name without ".milk"
        QString path;  // ":/milkdrop/..." or a file on disk
        bool builtIn = true;
    };

    // Built-in presets come first, then the user's (each sorted by name).
    // Missing folders are fine.
    void load(const QString& builtInDir, const QString& userDir);

    int size() const { return int(m_presets.size()); }
    bool isEmpty() const { return m_presets.isEmpty(); }
    const Preset& at(int index) const { return m_presets.at(index); }
    int indexOf(const QString& name) const;
    // The preset text, NUL-terminated (projectM wants a C string). Empty if unreadable.
    QByteArray data(int index) const;

    int next(int current) const;      // wraps around
    int previous(int current) const;  // wraps around
    int random(int current) const;    // a different one when there are several

private:
    QList<Preset> m_presets;
};

}  // namespace qiyaa
