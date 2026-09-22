# Traduzioni

Le stringhe QML sono marcate con `qsTr()` e quelle C++ con `tr()`/`QCoreApplication::translate()`.

Per aggiungere una lingua, generare un catalogo Qt TS con `lupdate`, tradurlo e compilare il relativo QM con `lrelease`. I cataloghi binari possono poi essere caricati all'avvio o incorporati nelle risorse Qt. La sorgente italiana resta il testo di riferimento per la release 0.7.2.