#
# $Id$
#
# G:15-Jan-2000
C:TelDa.Net
C:Address:Telefon-, Daten- und Fax-Transfer GmbH & Co. KG, Schuppertsgasse 30, 35083 Wetter (Hessen)
C:Address:TeDaFax, Telefon-, Daten- und Faxtransfer AG, Postfach 2206, 35010 Marburg
C:Homepage:http://www.teldafax.de
C:Hotline:0800/01030-23
C:Hotfax:0800/01030-33
C:Maintainer:Tarif Datenbank Crew <rates@gmx.de>
C:Special:Die Homepage der Tarif-Datenbank Crew: http://www.digitalprojects.com/rates
#
# ACHTUNG:
# Hierbei handelt es sich um das "CeBIT Sonderangebot" der TelDaFax
# Nach einer Anmeldung (Zugangsnummer: "CEBIT-M70F35QVYPGT76")
# kann dieser Dienst fr 3 Monate zum Preis von
# DM 0,029/Minute genutzt werden, danach kostet dieser Dienst
# DM 0,06/Minute
#
# Damit "isdnlog" diesen Tarif korrekt abrechnet, müssen folgende Schritte
# unternommen werden:
#
# 1. In der "/usr/lib/isdn/rate-de.dat" muß im Kapitel
#    "P:30,0 TelDaFax"
#    der Eintrag
#    "I:TelDa.Net.i" eingefügt werden.
#
# 2. Das untige Datum 1.6.2000 muß durch das persönliche Ablaufdatum für
#    dieses Sonderangebot abgeändert werden.
#
Z:109
A:08000103021
T:[-01.06.2000] */*=0.029/60
T:[01.06.2000]  */*=0.06/60
#####################################################################
##L $Log$
