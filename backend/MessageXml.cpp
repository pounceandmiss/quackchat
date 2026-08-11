#include "MessageXml.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

// Two spaces a level. The Tk viewer indents by one, but it also aligns wrapped
// attributes under the tag name; without that, one space hardly reads as depth.
constexpr int kIndent = 2;

} // namespace

QString formatMessageXml(const QString &stanza) {
    if (stanza.isEmpty())
        return stanza;

    QString out;
    QXmlStreamReader reader(stanza);
    // Off, so prefixes and xmlns declarations reach the output as they were
    // written. With it on, Qt resolves them and invents prefixes of its own -
    // a debug view that renames what it was handed is no use.
    reader.setNamespaceProcessing(false);
    QXmlStreamWriter writer(&out);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(kIndent);

    // Token by token rather than writeCurrentToken, which copies the local name
    // and so quietly drops the prefix off `<x:message>`. What is not named here
    // - document ends, a DTD - was never in a stanza, so it is left out.
    while (!reader.atEnd() && !reader.hasError()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            writer.writeStartElement(reader.qualifiedName().toString());
            const QXmlStreamAttributes attrs = reader.attributes();
            for (const QXmlStreamAttribute &a : attrs)
                writer.writeAttribute(a.qualifiedName().toString(),
                                      a.value().toString());
            break;
        }
        case QXmlStreamReader::EndElement:
            writer.writeEndElement();
            break;
        case QXmlStreamReader::Characters:
            writer.writeCharacters(reader.text().toString());
            break;
        case QXmlStreamReader::Comment:
            writer.writeComment(reader.text().toString());
            break;
        default:
            break;
        }
    }
    return reader.hasError() ? stanza : out;
}
