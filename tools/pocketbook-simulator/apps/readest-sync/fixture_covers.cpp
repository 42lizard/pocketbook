// Build-time artwork generation; runtime mock transport uses Qt Core only.
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <stdexcept>
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    if(argc!=2) return 2;
    for(int i=0;i<51;++i) {
        const auto title=QString("%1 · %2").arg(i+1,2,10,QChar('0')).arg(i==0?"Position playground":i%5==1?"Position only":"Sample library book");
        QImage cover(400,600,QImage::Format_RGB32); cover.fill(i%2?QColor("#dedede"):QColor("#f4f4f4"));
        QPainter painter(&cover); painter.setPen(Qt::black);
        painter.setFont(QFont("DejaVu Sans",26));
        painter.drawRect(20,20,359,559);
        painter.drawText(QRect(35,70,330,400),Qt::AlignCenter|Qt::TextWordWrap,title);
        painter.end();
        if(!cover.save(QString::fromLocal8Bit(argv[1])+QString("/%1.png").arg(i,2,10,QChar('0')),i%2?"JPEG":"PNG"))
            throw std::runtime_error("Cannot create fixture cover");
    }
}
