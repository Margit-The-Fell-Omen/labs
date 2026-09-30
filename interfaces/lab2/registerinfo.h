#pragma once

#include <QString>
#include <QVector>

struct BitField
{
    int bit;
    QString name;
};

struct RegisterField
{
    int offset;
    int size;
    QString name;
};

const QVector<BitField> &commandBits();
const QVector<BitField> &statusBits();
const QVector<RegisterField> &headerFields();