#!/usr/bin/perl
#
# Copyright 1999 by Leopold Toetsch <lt@toetsch.at>
#
# This program is free for private use. Private use means, you
# may install and run this program on your home/office computer(s).
# But you are not allowed to use this program on public webservers.
#
# For commercial usage on public webservers contact the author.
#
# Version 1.00 1999.08.30
#

use CGI qw(:standard);
use CGI::Carp 'fatalsToBrowser';
use strict;
use GD;
use IO::Handle;
# socket stuff
my $use_sockets=1;
use Socket;

# path for unix socket
my $server='/tmp/isdnrate';

# configure adjusts these n/y :-(

my $MKTEMP= '/bin/mktemp';
my $ISDNRATE='/usr/bin/isdnrate';
my $CODEF=   '/usr/lib/isdn/code-at.dat';

my $LOGO="(c) 1999 www.Tel-R.at";

my $docroot=$ENV{'DOCUMENT_ROOT'};

# for getting zone info, check ( from to ... ), to should be diff zones
# adjust this
my @probe_zones = qw ( 01 01 02245 0732 07667 05574 ); #AT
#my @probe_zones = qw ( 030 030 03302 033200 089 ); #DE

# rest should be ok

my $tempdir = "$docroot/tmp";
my $tempdir_url = '/tmp';

my $DEFLEN=60;

my $debug=0;

my $LEER='--kein--';
# end configurable

# cgi
my $q=new CGI;
# some security things
$CGI::POST_MAX=1000;
$CGI::DISABLE_UPLOADS=1;

# global data

my %towns = ($LEER => $LEER);
my (@countries);
my (%url, $len, $TITLE);
my ($weekly, $daily, @names, $help, $mix, $info);

# main

$|=1;
$TITLE="Telefonkosten";
$help=0;
$weekly=$daily=$help=$mix= $info=0;
@names=param();
foreach (@names) {
    my $v = param($_);
    $v =~ s/[^\w\d.:, +\-~�������]//g;
    $v =~ s/^-//; # no options in params
    param($_, $v)
}	

if ($debug) {
    &html_header('debug',1);
    print(hr, dump());
    print(hr);
}    

if (param('info')) {
    &html_header("$TITLE - Info",0);
    &info(param('info'));
}    
elsif (param('list')) {
    &html_header("$TITLE - Providerliste",0);
    &list(param('list'));
}   
else {
    $weekly=param('graf') =~ /Wo/;
    $daily=param('graf') =~ /Tag/;
    $mix=param('mix');
    my $subt = $mix?"Gespr�chsmix":$daily?"Tagesverlauf":
	$weekly?"Wochenverlauf":"Einzelgespr�ch";
    $subt = 'Hilfe' if(param('help'));	
    &html_header("$TITLE - $subt",$mix?0:1);
    if (((param('town') && param('town') !~ /--/) || param('gettown')) && !$mix) {
       &read_towns;
    }   
    if (((param('country') && param('country') !~ /--/) || param('getcountry')) && !$mix) {
       &read_countries;
    }   
    $help=0;
    push(@names, 'help_tel.x') if (@names && !param('tel') && 
	!param('town') && !param('country') && 
	!param('gettown') && !param('getcountry') && !param('mix') && !param('help'));
    foreach (@names) {
	if (/help_(.*?)\.x/) {
	    show_help($1);
    	    $help=1;
	    last;
	}	
    }
    if (param('help')) {
	$help=param('help');
	&help_all($help);
    }	
    if (param('clear')) { # clear mix
	my $n;
	param('mix',$mix=10);
        param('from','');
	foreach $n (0..$mix-1) {
	    foreach ('tel','oft','len','dday') {
		param("$_$n", '');
    	    } #for
	}
	param('best','20');	
	param('prov','');
	param('xprov','');
	param('_3D','on')
    }    
    if (!$help && ( (param()>1 && $mix) || param())) {    
	&print_table if(param('tab'));
	&make_graf if(param('graf'));
    }    
    if (param('more.x')) {
	$mix+=10 ;
	param('mix', $mix);
    }    
    if (!param() || ($mix && param()==1)) {
	param('best','20');	
	param('_3D','on');
	param('now','on');
	param('len',$DEFLEN);
    }	
    elsif ($help < 3) {
	print(p, img({-src=>"/telrate/hr.gif", -width=>600, 
	    -height=>4, -alt=>'-----'})
	, h3('Neue Eingabe'));
    }	
    &print_form if($help != 3);
    &clean_up;
    &footer();
}   
1;

# subs
# footer

sub footer {
    my($pnum) = $_[0];
    my($t);
    print(
	img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),p,
	div({-class=>'sm'},
	    'Alle Angaben ohne Gew�hr.',br,
	    'Die Tarife der Provider �ndern sich h�ufig und k�nnen daher ',
	    'eventuell falsch sein. In diesem Falle wenden ',
	    'Sie sich bitte'));
    if ($pnum && ($t=&get_info($pnum, 'Maintainer'))) {
	my ($e, $n);
	if ($t =~ /(.*?)\s*<(.*?)>/) {
	    $e = $t; $n = $1;
	}    
	else {
	    $e = $n = $t;
	}    
	print(span({-class=>'sm'},'an den Maintainer der Tarife ',
	    a({-href=>"mailto:$e?subject=Tarife $pnum"},$n),' oder'));
    }	
    print(	    
	span({-class=>'sm'},
	    ul(li('in �sterreich an ',
	    a({-href=>'mailto:reinelt@eunet.at?subject=Tarife'},
	    'Michael Reinelt')),
	    li('in Deutschland an ',
	    a({-href=>'mailto:rate-de@Joker.E.Ruhr.de?subject=Tarife'},
	    'die deutsche ISDN-Rate Crew'))),
	    'oder an den Autor dieses Pogrammes ',
	    a({-href=>'mailto:lt@toetsch.at?subject=Tarife'},'Leopold T�tsch'),'.'),
	    p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),p,
	    a({-href=>'/telrate/index.html'},'Startseite',
	    img({-src=>'/telrate/start.jpg', -border=>0, width=>80, 
	    height=>40,-hspace=>8, -alt=>'Startseite'})),
	    '&nbsp;','&nbsp;'
    );
    my $url= url();
    $url =~ s/&help.*?=\w+//g;
    if ($mix || $help == 3) {
	print(a({-href=>$url}, 'Einzelgespr&auml;ch',
	    img({-src=>'/telrate/eg0.jpg', -border=>0, width=>80, 
	    height=>40,-hspace=>8, -alt=>'Einzelgespr&auml;ch'})));
    }
    if (!$mix || $help == 3) {	    
	$url .= '?mix=10';
	print(a({-href=>$url}, 'Gespr&auml;chsmix',
	    img({-src=>'/telrate/gm0.jpg', -border=>0, width=>80, 
	    height=>40,-hspace=>8, -alt=>'Gespr&auml;chsmix'})));
    }	    
    print('&nbsp;&nbsp;&nbsp;',a({-href=>'/telrate/info.html'}, 'Info, Hilfe',
	    img({-src=>'/telrate/info0.jpg', -border=>0, width=>80, 
	    height=>40,-hspace=>8, -alt=>'Info'})));
    if (!param('list')) {
	$url =~ s/\?.*//g;
	$url .= '?list=1';
	print(a({-href=>$url}, 'Providerliste',
	    img({-src=>'/telrate/list0.jpg', -border=>0, width=>80, 
	    height=>40,-hspace=>8, -alt=>'Providerliste'})));
    }	    
    print(end_html);
}    

sub html_header {
    my($title, $use_script) = @_;
    my $script;
    print(header);
    if ($use_script) {
	$script = q(
    tim = 0;
    function start(but) {
	if(document.form.Start.value=='Start') {	
	    tim=setInterval("run()",1000);
	    document.form.len.value=0;
	    document.form.Start.value='Stop';
	    document.form.now.checked=false;
	    j=new Date();
	    document.form.day.value=j.getDate()+'.'+(j.getMonth()+1)+'.'+j.getYear();
	    document.form.hour.value=j.getHours()+':'+j.getMinutes()+':'+j.getSeconds();	
	}			
	else {
	    clearInterval(tim);
	    document.form.Start.value='Start';
    	}
    }
    function run() {
       document.form.len.value++;
    }   
	);#q
    }
    else {
	$script='';
    }		
    my $style = q(
<!--
.t {font-family:Sans Serif;font-size:10pt}
.sm {font-family:Sans Serif;font-size:9pt}
i {color:#000080}
h1,h2,h3,h4,p,td,th,body { font-family:Sans Serif,Arial }
-->
); 

    print(start_html(-title=>$title, -bgcolor=>'#f0f0f0',
	    -script=> $script, -head=>style({-type=>'text/css'},$style)),
	h1({-align=>'center'},$title));
}
    
sub read_towns {
   my($c, $t, $wanted);	
   if (param('town') && ($wanted=substr(param('town'),0,1)) ne '-') {
	open(IN, $CODEF) || print("Can't read $CODEF");	
	while (<IN>) {
	    chomp;
    	    ($c, $t) = split(/\t/,$_,2);
   	    $towns{$t}=$c if($t =~ /^$wanted/i);
       }
       close(IN);
    }
    for $c ('A'..'Z','�','�','�') {
	$towns{$c}=$c;
    }	      
}   

sub help {
  my $help = 'help_'. $_[0];
  image_button(-name=>$help, -src=>'/telrate/help.gif', 
      -border=>0, -alt=>'Hilfe', -tabindex=>999);
}  

sub show_help {
    my $what = $_[0];
    my $mixt =
	'<br>Ist <i>Mit Geb�hren/Monat</i> ausgew�hlt, werden in der letzten '
	.'Spalte allf�llige Geb�hren (Mindestumsatz usw.) aufgelistet. '
	.'Einmalige Geb�hren (Anmeldung, Freischaltung) werden auf 1 Jahr aufgeteilt.<br>'
	.'Provider, die nicht alle Dienste zu Verf�gung stellen, oder deren '
	.'Mindestumsatz nicht erreicht wird, werden normalerweise nicht angezeigt. '
	.'Wenn sie alle <i>Alle</i> angekreuzt haben, werden diese Provider '
	.'mit <i>**</i> markiert aufgelistet.';
    
    if ($what eq 'from') {
	print('Geben Sie hier die Vorwahl Ihres Standortes ein, ',
	    'dann werden die Geb�hren von dieser Vorwahl aus berechnet',
	    br,'Z.B. ', tt('02345'));
    }
    elsif($what eq 'tel') {
	print('Geben Sie die Nummer ein, zu der Sie die Geb�hren ',
	    'berechnet haben m�chten.',br,
	    ul(li(tt("1234\t"),'Ortsnetz'),
	    li(tt("012345\t"),'Anderer Ort'),
	    li(tt("00156789\t"),'Ausland'),
	    li(tt("+1 56789\t"),'Ausland'),
	    li(tt("China\t"),'Ausland'),
	    li(tt("1012 01 47110815\t"),'Provider+Nummer (damit wird aber nur dieser angezeigt)')),
	    'oder w�hlen Sie aus der Liste der St�dte oder L�nder.',br,
	    'Hinweis: eine ausgew�hlte Stadt hat Priorit�t vor einem Land, ',
	    'dieses vor einer manuellen Eingabe.', br,
	    'Wenn Sie ein anderes Land oder eine andere Stadt w�hlen m�chten ',
	    'selektieren Sie den gew�nschten Anfangsbuchstaben und dann ',i('GO'), 
	    ' damit wird auch die L�nder- bzw. St�dteliste f�r diesen Buchstaben ',
	    'gef�llt.',br
	    i('GO'),' dient zur schnellen Anwahl von ',i('Tabelle'),'.');
    }	    
    elsif($what eq 'len') {
	print('W�hlen Sie hier Dauer und Zeitpunkt des Gespr�chs. ',
	    'Die Dauer ist standardm��ig in Sekunden anzugeben, Sie  k�nnen ',
	    'aber auch durch Anh�ngen eines ',i('m'),' die Dauer in Minuten ',
	    'eingeben, z.B. ',tt(i('2m 33')),'.',br,
	    'Mittels ',i('Start'),' k�nnen Sie aktuelle Gespr�che mitstoppen ',
	    'und sich so die angefallenen Gespr�chsgeb�hren anzeigen lassen.',br,
	    'Wenn ',i('Jetzt'),' nicht angekreuzt ist, k�nnen Sie einen beliebigen ',
	    'Zeitpunkt eingeben. Ist kein Tag angegeben, wird heute angenommen. ',
	    'Ist auch keine Uhrzeit angegeben, gilt der Zeitpunkt der Auswahlbox.');
    }	
    elsif($what eq 'tab') {
	print('Mit der Schaltfl�che ',i('Tabelle'),' erhalten Sie eine ',
	    'Aufstellung der Telefongeb�hen pro Provider. ',
	    'Durck Klicken auf ',i('Nr.'),' bzw. ',i('Provider'),' bekommen ',
	    'Sie die Liste sortiert nach Zugangsnummer bzw. Providerbezeichnung - ',
	    'die Standardsortierung ist nach der Geb�hr.');
    }	
    elsif($what eq 'graf') {
	print('Mit der Schaltfl�che ',i('Grafik'),' erhalten Sie eine ',
	    'grafische Darstellung des Verlaufs der Telefongeb�hen bis ',
	    'zur gew�hlten Dauer. Damit sehen Sie sehr �bersichtlich, ',
	    'ob der Provider Sekundentakt oder einen anderen vewendet.', 
	    br
	    'Um kleinere Bilder (schneller) zu erhalten, verringern Sie die Gr��e ',
	    'und/oder schalten Sie ',i('3D'),' aus.');
    }	
    elsif($what eq 'tag') {
	print('Die Schaltfl�chen ',i('Tag'),' und ',i('Woche'),' zeigen die ',
	    'Geb�hren f�r die gew�hlte Dauer und den gew�hlten Tag im Verlauf ',
	    'eines Tages bzw. einer Woche. Dabei werden die Geb�hren am Tag (8-18h) ',
	    'in der Reihung der Provider st�rker gewichtet, als in der �brigen Zeit.');
    }
    elsif($what eq 'prov') {
	print('W�hlen Sie hier welche und wie viele Provider angzeigt werden. ',
	    'Sind ',i('nur Provider'),' angegeben, werden nur diese angezeigt. ',
	    'Sind ',i('nicht Provider'),' angegeben, werden diese nicht angezeigt.',
	    br,'Hinweis: wenn Sie mehrere Provider eingeben, trennen Sie diese ',
	    'mit einem Beistrich oder Leerzeichen. Sie k�nnen die ',
	    'Providernummer eingeben z.B. ',tt('01033,010050'),' oder auch in ',
	    'einer abgek�rzten Variante, wobei Sie in Deutschland bei den ',
	    '6-stelligen Providernummern 100 addieren, also ',tt('33,150'),
	    ' f�r obiges Beispiel.',p,
	    'Bei ',i('Taktdauer'),' k�nnen Sie angeben, da� nur Provider mit einer ',
	    'Taktung kleiner-gleich dem gew�hlten Wert ', 
	    'angezeigt werden. Beachten Sie, da� ',
	    'bei Providern, die eine uneinheitliche Taktung ',
	    'verwenden, der Takt am Ende der Gespr�chsdauer verwendet wird.', 
	    p,
	    i('Reset'),' stellt den vorhergehenden Zustand wieder her, ',
	    i('L�schen'),' l�scht alle Eingabefelder, bzw. stellt Standardwerte ein.');
    }
    elsif($what eq 'mix') {
	print('Geben Sie die Nummern ein, zu der Sie die Geb�hren ',
	    'berechnet haben m�chten.',br,
	    ul(li(tt("1234\t"),'Ortsnetz'),
		li(tt("012345\t"),'Anderer Ort'),
		li(tt("00156789\t"),'Ausland'),
		li(tt("+1 56789\t"),'Ausland'),
		li(tt("China\t"),'Ausland')),
	    'Sie brauchen nat�rlich nicht jedes Ziel eingeben, sondern nur ',
	    'Vorwahlen in unterschiedlichen Entfernungen, bzw. in verschiedenen ',
	    'Zonen.',p,	
	    'Und wie oft Sie im Zeitraum *), wie lange (in Sekunden), bzw. ',
	    'mit nachgestelltem ',i('m'),' z.B. ',tt(i('2m10')),
	    ' &nbsp;in Minuten/Sekunden dorthin ',
	    'zu welcher Tageszeit durchschnittlich telefonieren.',br,
	    'Wenn Sie ein Tildezeichen ',tt(i("'~'")),' vor die Zeitdauer setzen ',
	    'werden maximal 20 Zufallsl�ngen +/- 50 Prozent von Ihrer gew�nschten ',
	    'Zeitdauer berechnet, z.B. bei ',tt(i('~1m')),' werden Gespr�chsl�ngen von ',
	    '30s bis 1m30s verwendet.',
	    br,
	    'Wenn Sie mehr Eingabefelder ben�tigen, klicken Sie auf ',
	    i('Mehr'),'.',p,
	    '<SUP>*)</SUP> Wenn sie ',i('Mit Geb�hren/Monat'), ' gew�hlt haben ',
	    'sollten Sie die Anzahl der Gespr�che in einem Monat eingeben, ',
	    'da die Geb�hren pro Monat berechnet werden.');    
    }	    
    elsif($what eq 'tabm') {
	print('Sie erhalten eine Tabelle mit den Gesamtkosten ',
	    'sowie mit den einzelnen Teilen Ihres Gespr�chsmix.',
	    $mixt);
    }	    
    elsif($what eq 'grafm') {
	print('Sie bekomme eine grafische �bersicht �ber Ihren ',
	    'gew�hlten Gespr�chsmix. Jede Stufe im Diagramm zeigt ',
	    'die Geb�hren f�r den jeweiligen Dienst. ',
	    'Rechts sehen Sie die Gesamkosten f�r alle Gespr�che.',
	    $mixt,
	    br,
	    'Um kleinere Bilder (schneller) zu erhalten, verringern Sie die Gr��e ',
	    'und/oder schalten Sie ',i('3D'),' aus.');
    }	    
    if ($what =~ /tab|graf/) {
	print(br,'Sie k�nnen auf die Providerbezeichnung ',
	    'klicken um weitere Information �ber den jeweiligen Provider zu ',
	    'erhalten.');
    }	    
    if ($help == 0) {
	my ($url) = self_url;
	$url =~ s/&help.*?=\w+//g;
	$url .= '&help=';
	$url .= $mix?'2':'1';
	print(p,'Hilfe �ber alle Eingabem�glichkeiten gibt es ',
	    a({-href=>$url}, 'hier'),'.');
    }	    
}   

sub help_all {
    my ($what) = $_[0];
    my ($e, %allh, @allh);
#    print(h3('Allgemein'));
    if ($what & 1) {
	%allh = (from => 'Von wo w�hlen Sie', tel => 'Wohin telefonieren Sie',
	    len => 'Wie lange', tab => 'Tabelle', graf => 'Grafik', 
	    tag => 'Tag / Woche', prov => 'Provider');
	@allh = qw( from tel len tab graf tag prov );	
	print(h2('Einzelgespr�ch'));
	foreach $e (@allh) {
	    print(h3($allh{$e})); 
	    show_help($e);
	}	
    }	
    if ($what & 2) {
	%allh = (from => 'Von wo w�hlen Sie', mix => 'Wohin telefonieren Sie',
	    tabm => 'Tabelle', grafm => 'Grafik', 
	    prov => 'Provider');
	@allh = qw( from mix tabm grafm prov );	
	print(h2('Gespr�chsmix'));
	foreach $e (@allh) {
	    print(h3($allh{$e})); 
	    show_help($e);
	}	
    }	
    param('help');
    print(p,br);
}

sub byname {
    my($ca, $cb) = ($a, $b);
    foreach ($ca,$cb) {
	s/�/Ae/;
	s/�/Oe/;
	s/�/Ue/;
    }
    $ca cmp $cb;	
} 	
sub print_form {
    my($t,  $i);
    my @codes = sort byname (keys(%towns));
    my $t= q!print(
    	start_form(-name=>'form'),
	table({-border=>8},
	    Tr(td(table({-bgcolor=>'#ffffe0', -cellspacing=>0, -cellpadding=>0, -border=>0}, 
	    Tr({-bgcolor=>'#ffffa8'},td(['&nbsp;', b('Ich w�hle von: ')]),
    		td({-colspan=>2},textfield(-name=>'from', -size=>12, -maxlength=>20)),
		td(['&nbsp;', &help('from')])), !;
    if ($mix) { 
	$t .= q!
	    Tr({-bgcolor=>'#ffffa8'},[
		td({-colspan=>6},hr),
		td(['&nbsp;',b('Und telefoniere nach&nbsp;&nbsp;&nbsp;'),
		    b('so oft&nbsp;&nbsp;&nbsp;'),
		    b('so lang <span class="sm"><i>(s)</i></span>&nbsp;&nbsp;'),
		    b('am'),&help('mix')])]),!;
	for $i (0..$mix-1) {
	    my($ii)=$i+1;
	    my ($col) ;
	    $col=sprintf("'#ffff%2x'", (0xa8 + $ii*4) > 0xe0 ? 0xe0 : (0xa8 + $ii*4));
	    $ii=qq('$ii&nbsp;&nbsp;');
	    $t .= q!
	    Tr({-bgcolor=>!."$col".q!},
		td([! . "$ii" .q!,
		    textfield(-name=>"tel! .$i .q!", -size=>12, -maxlength=>20),
		    textfield(-name=>"oft! .$i .q!", -size=>4, -maxlength=>4),
		    textfield(-name=>"len! .$i .q!", -size=>8, -maxlength=>8),
		    popup_menu(-name=>"dday! .$i .q!", -values=> ['W','N'],
		    	-labels=> {'W' =>'Tag','N'=>'Nacht'}),
		    !;
	    $t .= ($i==$mix-1) ? q!image_button({-name=>'more', -src=>'/telrate/more.gif', 
              -border=>0, -alt=>'Mehr Felder'})! : q!'&nbsp;'!;
	    $t .= q!])),!;
	} # for
    } # if mix	
    else {    
	$t .= q!Tr({-bgcolor=>'#ffffa8'},[
	    td({-colspan=>6},hr),
    	    td(['&nbsp', '<b>nach</b> TelefonNummer: &nbsp;',
    	    textfield(-name=>'tel', -size=>12, -maxlength=>20),'&nbsp;',
		submit('tab','GO'),
			&help('tel')])]),
	!;
	# town/gettown
	my $cs=2;
	$t .= q!Tr(		
		td(['&nbsp;','oder Stadt: ']),!;
	if(param('town') || param('gettown')) {	
	    $cs = param('gettown') ? 1: 2;
    	    $t.= q!td({-colspan=>!. $cs. q!},popup_menu(-name=>'town', -values=> \@codes)),!;
	}   
	if(!param('town') || length(param('town'))==1 || param('gettown')) {	
    	    $t .= q!td(submit(-name=>'gettown',-value=>'St�dteliste')),!;
	    $cs++;
	}
	while ($cs++<7) {
	    $t.= q! td('&nbsp;'),!;
	}
	$t .= '),';
	# country/getcountry
	my $cs=2;
	$t .= q!Tr(		
		td(['&nbsp;','oder Ausland: ']),!;
	if(param('country') || param('getcountry')) {	
	    $cs = param('getcountry') ? 1: 2;
    	    $t.= q!td({-colspan=>!. $cs. q!},popup_menu(-name=>'country', -values=> \@countries)),!;
	}   
	if(!param('country') || length(param('country'))==1 || param('getcountry')) {	
    	    $t .= q!td(submit(-name=>'getcountry',-value=>'L�nderliste')),!;
	    $cs++;
	}
	while ($cs++<7) {
	    $t.= q! td('&nbsp;'),!;
	}
	$t .= '),';
	$t .= q!
	Tr([    
	    td({-colspan=>6},hr),
	    td(['&nbsp;','<b>Dauer</b> <span class="sm"><i>s</i> o. <i>2m 33</i></span>',
    		textfield(-name=>'len', -size=>6, -maxlength=>6),
		'Stoppuhr',
		button(-name=>'Start',-value=>'Start', -onClick=>'start()'),
		&help('len')]),
	    td(['&nbsp;',checkbox(-name=>'now', -label=>'Jetzt  - oder am'),
    		popup_menu(-name=>'dday', -values=> ['W','N','E'],
		    -labels=> {'W' =>'Werktag','N'=>'Nacht','E'=>'Sonntag'}),
		'&nbsp;','&nbsp;','&nbsp;']),
	    td(['&nbsp;','oder um <span class="sm"><i>hh[:mm[:ss]]</i></span> ',
    		textfield(-name=>'hour', -size=>8, -maxlength=>8),
    		'am <span class="sm"><i>dd[.mm[.jj]]</i></span> &nbsp; ',
    		textfield(-name=>'day', -size=>10, -maxlength=>10),'&nbsp;']),
	]),	
	    !;
    } # else mix   	
    $t .= q!Tr([    
	    td({-colspan=>6},hr),
	    td({-bgcolor=>'#ff80c0', colspan=>6, align=>'center'},b('Ausgabe')),
	    td({-bgcolor=>'#ffc080'},['&nbsp;',
    		submit('tab','Tabelle'), 
	!;
    if ($mix) {
	$t .= q!	
		'mit', 
    		popup_menu(-name=>'cost', -values=> ['0','1'],
		    -labels=> {'0' =>'Nur Gespr�chskosten','1'=>'Mit Geb�hren/Monat'}),
		checkbox(-name=>'all',label=>'Alle'),
		&help('tabm'),hidden(-name=>'mix', value=>$mix)]),
	!;
    }
    else {        	
	$t .= q!'&nbsp;','&nbsp;','&nbsp;',	
		&help('tab')]), 
	!;
    }
    $t .= q! 
	    td({-bgcolor=>'#87cefa'},[ '&nbsp;',		
    		submit('graf','Grafik'), 
		'Gr��e',
    		popup_menu(-name=>'swidth', -values=> ['1024','800','640'],
		    -labels=> {'1024' =>'gro�','800'=>'mittel','640'=>'klein'}),
		checkbox(-name=>'_3D',label=>'3D'),
		&help('grafm')]),
	!;
    if (!$mix) {
	$t .= q!	
    	    td({-bgcolor=>'#ffdead'},[
		'&nbsp;',
    		submit('graf','Tag'),
		"Tagesverlauf",
		'&nbsp;',
		'&nbsp;',
		&help('tag')]),
    	    td({-bgcolor=>'#eeb0ee'},[
		'&nbsp;',
    		submit('graf','Woche'),
		"Wochenverlauf",
		'&nbsp;','&nbsp;','&nbsp;'
		]), 
	!;	
    }
    $t .= q!	
	    td({-colspan=>6},hr),
    	    td(['&nbsp;','der besten ',
    		textfield(-name=>'best', -size=>2, -maxlength=>2),
		'Provider',
		,'&nbsp;',
		&help('prov')])
	]),
	Tr(	
	    td(['&nbsp;','oder nur Provider']),
	    td({-colspan=>2},textfield(-name=>'prov', -size=>12, -maxlength=>100)),
	    td(['&nbsp;','&nbsp;'])
	),
	Tr(	
	    td(['&nbsp;','oder nicht Provider']),
	    td({-colspan=>2},textfield(-name=>'xprov', -size=>12, -maxlength=>100)),
	    td(['&nbsp;','&nbsp;'])
	),
	Tr(
	    td(['&nbsp;','mit Taktdauer <=',
    		popup_menu(-name=>'takt', -values=> ['9999','60','30','10','1'],
		    -labels=> {'9999'=>'Alle','60' =>'1 m','30'=>'30 s','10'=>'10 s','1'=>'1 s'}),
		reset('Reset'), 
    !;
    if ($mix) {
    	$t .= q!submit('clear','L�schen'),!;
    }
    else {    
        $t .= q!defaults('L�schen'),!;
    }	
    $t .= q!    
	    '&nbsp;'])
	)
	)))),
    	end_form);
    !;
    eval($t);
    # uff
}	

sub del1_vbn {
    my $n = $_[0];
    if ($n =~ s/^0?10//) {
        $n=100+$n if (length($n) == 3);
    }    
    $n;
}    
sub del_vbn {
    my @p = split(/,/, $_[0]);
    my (@np, $ret);
    foreach (@p) {
	$_=del1_vbn($_);
	push(@np, $_);
    }
    $ret=join(',',@np);
    $ret;
}    	
sub parse_len {
    my $l = $_[0];
    if ($l =~ /(\d+)\s*m\s*(\d+)\s*s?/) {
	60*$1+$2;
    }
    elsif ($l =~ /(\d+)\s*m/) {
	60*$1;
    }
    else {
	$l =~ /(\d+)/;
	$1;
    }
}    	
sub  _call_isdnrate {
    my ($lines, @args) = @_;
    print "<pre>@args</pre>" if($debug);
    if ($use_sockets) {
	socket(SOCK, PF_UNIX, SOCK_STREAM, 0) || die("socket: $!");
	connect(SOCK, sockaddr_un($server)) || die("connect: $!");
	SOCK->autoflush(0);
	shift(@args);
	foreach (@args) {
	    print SOCK "$_ ";
	}
	SOCK->autoflush(1);
	my $line;
	while (defined($line = <SOCK>)) {
	    push(@$lines, $line);
	}    
	close(SOCK);
    }
    else {
	open(PIPE, "-|") || exec(@args) == 0 or die "Can't @args: $?";
        @$lines = <PIPE>;
        close(PIPE);
    }  
}
    
sub call_isdnrate {	
    my ($hour, $day);
    my ($lines) = @_;
    my ($now, $tel, $from, $best, $prov,$takt, $sort);
    if ($towns{param('town')} =~ /\d+/ && !$mix) {
	param('country', '') if(param('country'));
	$tel = '0' . $towns{param('town')};
	param('tel',$tel);
    }
    elsif (length(param('country'))>1&&param('country') !~ /--/ && !$mix) {
	param('tel','');
	param('town', '') if(param('town'));
	$tel=param('country');
    }
    else {
	param('country','') if (param('country') =~ /--/);	
	param('town','') if (param('town') =~ /--/);	
	$tel = param('tel');
    }	
    $tel =~s/ /_/g; # preserv spaces
    return if($tel eq '');		
    my @args=($ISDNRATE,"-H", $tel);
    unless (param('now')) {
    	push(@args, "-h$hour") if ($hour=param('hour'));
    	push(@args, "-d$day") if ($day=param('day')||param('hour')?param('day'):param('dday'));
    }	
    if (($prov=param('xprov')) && $prov =~ /\d/) {
	$prov =~ s/\s+/,/g;
	$prov=&del_vbn($prov);
	push(@args, "-x$prov");
    }	
    elsif (($prov=param('prov')) && $prov =~ /\d/) {
	$prov =~ s/\s/,/g;
	$prov=&del_vbn($prov);
	push(@args, "-p$prov");
    }	
    if ($from=param('from')) {
	$from =~ s/\s//g;
	push(@args, "-f$from") ;
    }	
    if (param('graf') && !$mix) {
	push(@args,$weekly? '-G98':$daily?'-G97':'-G99');
    }
    else {  
	push(@args, "-L"); # list
    }  
    push(@args,"-t$takt") if(($takt=param('takt')) <= 60 && !$info);
    $len=&parse_len(param('len')) || $DEFLEN;
    $len=&min($len, param('graf') ? 1200 : 3600);
    $len=&max($len, 10) if (param('graf'));
    param('len', $len);
    push(@args, "-l$len");
    if ($sort=param('sort')) {
	push(@args, "-S$sort");
	param('sort', '');
    }	
    if (!$mix) {
        $best=param('best') || 20;
  	param('best', $best>0 ? $best: 20); 
	push(@args, "-b$best") ;
    }	
    _call_isdnrate($lines, @args);
}

sub round {
    $_[0] == 0 ? '<tt>--.--</tt>': sprintf("%.03f", $_[0]);
}    

sub eval_cost {
    my ($pnum, $ch, $maxch) = @_;
    my (@lines, $f);
    $pnum = del1_vbn($pnum);
    my @args=($ISDNRATE,"-p$pnum -XGF $probe_zones[0]");
    _call_isdnrate(\@lines, @args);
    chomp($lines[0]);
    $f = (split(/  +/,$lines[0]))[3];
    if ($f) {
	my ($Cost, $Ch, $ret, $MaxCh);
	$Ch = $ch;
	$MaxCh = $maxch;
	$f =~ s/([a-zA-z]\w*)/\$$1/ig; # vars
	$f =~ s/`//g; # no` backticks
	$ret = eval($f);
	print pre("$pnum: $f ($Cost)") if($debug);
	($ret, round($Cost));
    }	
    else {
	(0,0);
    }
}    
sub call_mix {
    my ($lines) = @_;
    my ($n, $N);
    my (%pcost, %ptot, $ch, %pstring, %lines);
    my ($pnum, $prov, $cur, $charge, $rest);
    # kill empty 
    foreach $n (0..$mix-1) {
	next if(param("tel$n") && param("oft$n") && param("len$n"));
	# $n is empty
	foreach $N ($n+1..$mix-1) {
	    if(param("tel$N") && param("oft$N") && param("len$N")) {
		foreach ('tel','oft','len','dday') {
		    param("$_$n", param("$_$N"));
		    param("$_$N", '');
		} #for
		last;
	    }#if
	}    	    
    }	
    $N=0;
    foreach $n (0..$mix-1) {
	my(@one, $len, $oft, $first, $ca, $totlen, $o, $olen, $ooft);
	last unless(param("tel$n") && ($ooft=$oft=param("oft$n")) && ($len=param("len$n")));
	param('tel', param("tel$n"));
	param('dday', param("dday$n"));
	$first=1;
	$o=1;
	if ($len =~ /^~(.*)/) {
	    $ca=1;
	    $olen=parse_len($1);
	    $o = min(20,$oft);
    	    $oft = int($oft/$o) || 1;
	}
	$totlen=0;
	while(1) {    
	    if ($ca) {
		$len = max(10,min(1200,int($olen/2 + rand($olen))));
		if ($totlen+$oft*$len+10 >= $ooft*$olen) {
		    if ($oft > 1) {
			$oft =int($oft/2) || 1;
			next;
		    }	
		    $len = int(($ooft*$olen-$totlen)/$oft);
		}
	    }
	    $totlen += $oft*$len;
#print(pre("O=$o, OFT=$oft, LEN=$len, OLEN=$olen,TOT=$totlen\n"));	
	    param('len', $len);
	    @one=();
	    &call_isdnrate(\@one);
    	    shift(@one); # -H
	    shift(@one); # empty
	    foreach (@one) {
    		($pnum, $prov,undef,undef,undef, $cur, $charge) = &split_line($_);
    		$ptot{$pnum} += ($ch=&round($charge * $oft));
		$pcost{$pnum}[$N]+= $ch;
		if ($first) {
		    $pstring{$pnum} = $prov;
		    $lines{$pnum}++;
		} # $first
	    }	
	    last if($ca && $totlen >= $ooft*$olen);
	    last unless($ca);
	    $first=0;
	}    
	$N++;
    }
    param('mix', &min($N+5,$mix));
    $len=$N+1;
    my($ign1, $ign2);
    foreach $pnum (keys(%lines)) {
	$pstring{$pnum} = '*) '.$pstring{$pnum} , $ign1++ if($lines{$pnum}<$N);
    }	
    my ($maxch, $i);
    if (param('tab')) {
	if (param('cost') >0) {
	    foreach $pnum (keys(%ptot)) {
		next if($pstring{$pnum} =~ m/^\*\*/ && !param('all'));
    		foreach (@{ $pcost{$pnum} }) {
		    $ch = &round($_);
		    $maxch=max($maxch, $ch);
		}    
	        $ch = $ptot{$pnum};
		my ($ret, $cost) = eval_cost($pnum, $ch, $maxch);
		$ign2++, $pstring{$pnum} = '**) '.$pstring{$pnum} if($ret == -1);
		$pcost{$pnum}[$N] = $cost;
		$ptot{$pnum} += $cost;
	    }	
	    $N++;$len++;
	}    
	$n=0;
	foreach $pnum (sort { $ptot{$a} <=> $ptot{$b} } (keys(%ptot))) {
	    next if($pstring{$pnum} =~ m/^\*/ && !param('all'));
	    last if(++$n > param('best'));
	    $rest = '';
    	    for $i (0..$N-1) {
		$ch = $pcost{$pnum}[$i];
		$ch = round($ch) if($ch>0);
		$rest .= qq(</font></td><td align="right"><font size=-1><b> $ch </b>);
	    }		
	    $prov = $pstring{$pnum};
	    $ch = $ptot{$pnum};
	    push(@$lines, "$pnum;$prov;;;;$cur;$ch;$rest"); 
	}
    }	
    else { # graf
	my($c1);
	if (param('cost') > 0) {
	    foreach $pnum (keys(%ptot)) {
		$maxch=0;
		$ch=0;
    		foreach $n (0..$N-1)  {
		    $ch += ($c1=0+&round($pcost{$pnum}[$n]));
		    $maxch=max($maxch, $c1);
		}    
		my ($ret,$cost) = eval_cost($pnum, $ch, $maxch);
		$pstring{$pnum} = '** '. $pstring{$pnum}, $ign2++ if($ret == -1);
		push(@{$pcost{$pnum}}, $cost);
	    }	
	    $N++; $len++;
	}    
	$n=0;
	foreach $pnum (keys(%ptot)) {
	    next if($pstring{$pnum} =~ m/^\*/ && !param('all'));
	    last if(++$n > param('best'));
	    push(@$lines, "@ $pnum"); # start
	    $ch = 0;
    	    foreach $i (0..$N-1)  {
		$ch += 0+&round($pcost{$pnum}[$i]);
		push(@$lines, "$i $ch");
	    }	
	    push(@$lines, "$N $ch");
	    $prov = $pstring{$pnum};
	    push(@$lines, "@---- $cur $prov"); # end
	}
    }
    ($ign1, $ign2);
}

sub dig2str {
    $_[0] > 3 ? $_[0] : ('Ein','Zwei','Drei')[$_[0]-1];
}

sub print_ign {
    my ($ign1, $ign2) = @_;
    print(p,"*) ", dig2str($ign1),' Provider biete',$ign1>1?'n':'t',
	' nicht alle gew�nschten Zielorte an. ') if ($ign1);
    print(br,"**) ", dig2str($ign2),' Provider erreich',$ign2>1?'en':'t',
	' den Mindestumsatz nicht. ')  if ($ign2);
    print (p,'Um die nicht angezeigten Provider zu sehen, w�hlen Sie bitte ',
	i('Alle'),'.') if(($ign1 || $ign2) && !param('all'));	
}

sub print_table {
    my (@lines);
    my($pnum, $prov, $cur, $charge, $bgcolor, $i, $rest, $url, $ign1, $ign2);
    if ($mix) {
	($ign1, $ign2) = &call_mix(\@lines);
    }
    else {	
	&call_isdnrate(\@lines);
        $lines[0] = &fmt_date($lines[0]);
	print(p({-class=>'t'},$lines[0]));
        shift(@lines); # -H
	shift(@lines); # empty
    }	
    (undef,undef,undef,undef,undef,$cur)=&split_line($lines[0]);
    print("<table cellpadding=3><tr>");
    if ($mix) {
	foreach ('Nr.', 'Provider',$cur,
	    '- % <A href="#hint"><SUP>1)</SUP></A>','Z.') {
    	    print("<th>$_</th>");
	}    
	for($i=1;$i<$len-(param('cost')>0);$i++) {
	    print("<th>$i</th>");
	}
	print("<th>Geb�hren</th>") if (param('cost')>0);
    }	    
    else {
	print("<th>");
	$url = self_url;
	$url =~ s/&sort=.//g;
	print(a({-href=>$url ."&sort=v"}, 'Nr.'));
	print("</th><th>");
	print(a({-href=>$url ."&sort=n"}, 'Provider'));
	print("</th><th>");
	print(a({-href=>$url}, $cur));
	print("</th>");
	foreach ('- % <A href="#hint"><SUP>1)</SUP></A>',
	    ' /min <A href="#hint"><SUP>2)</SUP></A>',
	    'Takt <A href="#hint"><SUP>3)</SUP></A>',
	    'Zone','Tag','Zeit') {
	    print("<th>$_</th>");
	}
    }	    
    print("</tr>\n");
    $i=0;
    my $max_ch=0;
    foreach (@lines) {
	(undef,undef,undef,undef,undef,undef,$charge) =  &split_line($_);
	$max_ch = $charge if($charge > $max_ch);
    }	
    foreach (@lines) {
	my ($mind,$unit,$mp,$zone,$dur,$day,$time,$takt);
    	($pnum,$prov,$zone,$day,$time,$cur,$charge,$mind,$unit,$dur,$mp,$takt) = &split_line($_);
	$takt =~ s/(\d+\.\d?)0+/$1/g;	
	if (!$mix) {
	    $mind=round($mind); $mp=round($mp);	
	    $mp = "$mind + $mp/m"if($mind > 0);
	}
	else {
	    $rest=$mind;
	}    
	$charge=&round($charge);
	$url{$pnum}=self_url ."&info=$pnum";
	$url=a({-href=>$url{$pnum}}, $prov);
	$bgcolor=++$i&1?' bgcolor="#fffff0"':'';
	print(qq(<tr$bgcolor><td>$pnum</td><td>$url</td><td align="right">$charge</td>));
	my $perc = $max_ch > 0 ? int(($max_ch - $charge)/$max_ch*100) : ' ';
	print(qq(<td align="center"><font size="-1">$perc</font></td>));
	if ($mix) {
	    print(qq(<td><font size="-1">$rest</font></td>));
	}
	else {
	    my $ali = 'center';
	    my $j;
	    foreach($mp, $takt,$zone,$day,$time) {
		print(qq(<td align="$ali"><font size="-1">$_</font></td>));
		$ali = 'left' if(++$j == 2);
	    }    
	}    
	print("</tr>\n");
    }
    print("</table>\n",p);
    print(span({-class=>'t'},
	a({-name=>'hint'},'<SUP>1)</SUP>'), ' Prozentwert Ersparnis zum teuersten ',
	'angezeigten Provider.'));    
    if ($mix) {
	print_ign($ign1, $ign2);
    }
    else {
	print(span({-class=>'t'},
	,br,
	'<SUP>2)</SUP>', ' Die aktuellen Verbindungspreise k�nnen durch die Taktung ',
	'h�her sein, als die theoretischen Minutenpreise. Das f�llt besonders ',
	'eklatant auf, wenn Sie als Zeitdauer ',i('1m'),' w�hlen.',br,
	'<SUP>3)</SUP> Bitte beachten Sie, da� Wechsel in der Taktung ',
	'z.B. um 18<SUP>00</SUP> in der Tabelle ',
	'nicht angezeigt werden, diese aber sehr wohl in der Berechnung ',
	'ber�cksichtig werden.',br,
	'Eine sch�ne �bersicht �ber die Taktung erhalten Sie auch mit',
	i('Grafik'),'.'));
    }	

}
sub parse_explain {
    $_[0] =~
	m!(((\d+\.)?\d+)\s\S+\s\+\s)? # mind 2
	    ((\d+\.)?\d+) # unit 4
	    .*?/((\d+\.)?\d+)s # cur/dur 6
	    \s=(\s((\d+\.)?\d+)\s\S+\s\+)? #8
	    \s((\d+\.)?\d+) # mp 11
	    \s.*?\( # explain
	    ([^,]+) # zone? 13
	    ,\s(\S+)(\s\(.+?\))? # day day? 14
	    (,\s?(.+?))?\)!x; # time 17
    ($2, $4, $6, $11, $13, $14, $17);
}

sub split_line {
    split(/;/, $_[0]);
}
   
sub fmt_date {
    my %days=(Mon=>'Mo',Tue=>'Di',Wed=>'Mi',Thu=>'Do',Fri=>'Fr',Sat=>'Sa',Sun=>'So');
    my %mons=(Mar=>'M�r',May=>'Mai',Oct=>'Okt',Dez=>'Dez');
    my ($db, $m, $d, $t, $y, $k);
    if ($_[0] =~ m/(\d+) Sekunden/) {
        $k = $&;
    	$d = $1;
	if ($d >= 60) {
	    $m = int($d / 60);
	    $d = $d % 60;
	    my $mn = $m > 1 ? 'n' : '';
	    my $sn = $d > 1 ? 'n' : '';
	    my $st = $d ? " $d Sekunde$sn": '';
	    $_[0] =~ s/$k/$m Minute$mn$st/;
	}
    }	    	
    if ($_[0] =~ s/(\w{3}) (\w{3}) \s?(\d\d?) (\S{8}) (\d{4})//) {
    	($db, $m, $d, $t, $y) = ($1,$2,$3,$4,$5);
    	$db=$days{$db};
    	$m=$mons{$m} ? $mons{$m} : $m;
	$t = '' if($daily);
    	$_[0] .= "$db $d. $m $y um $t" unless($weekly);
    }	
    $_[0];
}        

sub min {
 $_[0] < $_[1] ? $_[0]	: $_[1];
}
sub max {
 $_[0] > $_[1] ? $_[0]	: $_[1];
}
sub fmts {
    my($h,$m,$s);
    $s = $_[0];
    $h = int($s/3600); $s -= $h*3600;  $h='' unless($h); $h .= 'h' if($h);
    $m = int($s/60); $s -= $m*60; $m='' unless($m);$m .= 'm' if($m);
    $s='' unless($s);$s .= 's' if($s);
    "$h$m$s";
}
my($H, $xo, $yo, $xs, $ys, $sy, $DEP);
sub make_graf {	
    my ($W,$LEG,$LIN,$white,$black,$lgrey,$dgrey,$llgrey,$borcol,$tempf,$i);
    my (@lines, $n, $dx, @rawcolors, @colors, %pstring, %unused);
    my ($prov, $cur, $charge, $pnum, %pc, %pt, $r, $g, $bl);
    my ($swidth, %dim);
    $swidth=param('swidth')||1024;
    # dimensions
    %dim = (1024=>[600,300,190], 800=>[500,240,180],640=>[300,200,180]);
    $W=$dim{$swidth}[0]; 
    $H=$dim{$swidth}[1]; 
    $LEG=$dim{$swidth}[2];
    $DEP=$H/2;
    $LIN=10;
    $dx = 10;
    $xo=35; $yo=20;
    # make some colors
    $n=0;
    foreach $g ('00','33','66','99','aa') {
    	foreach $r ('00','33','66','99','aa','dd') {
    	    foreach $bl ('00','33','66','99','aa','dd') {
		$i= ++$n*17 % (6*6*5);
		next if($r eq $g && $r eq $bl);
	    	push(@rawcolors, "$i-0x$r-0x$g-0x$bl");
	    }
	}
    }
    @rawcolors=sort(@rawcolors);		
    # get data
    my ($text);
    my ($ign1, $ign2);
    if ($mix) {
	($ign1, $ign2) = &call_mix(\@lines);
    }
    else {	
	&call_isdnrate(\@lines);
    }	
    foreach (@lines) {
	if (/^\@--+ (\S+) (\S.*)/) { # end
	    if (($pnum && $#{ $pc{$pnum} } == 0) || $2 eq '(null)') { # any data
	        pop( @{ $pt{$pnum} } );
	        pop( @{ $pc{$pnum} } );
		delete $pstring{$pnum};
		$unused{$pnum}++;
		next;
	    }		
	    $pstring{$pnum} = $2;
	    $cur=$1;
	    if ($daily || $weekly) {
	        push( @{ $pt{$pnum} }, $daily?24:7*24); # time
		push( @{ $pc{$pnum} }, $pc{$pnum}[0]); # charge
	    }	
	    $url{$pnum}=self_url ."&info=$pnum";
    	    $pnum = '';
	}
        elsif (/^\@ (\d+)/) { # start
    	    $pnum=$1;
	    if (!$daily && !$weekly && !$mix) {
    		push( @{ $pt{$pnum} }, 0); # time
		push( @{ $pc{$pnum} }, 0); # charge
	    }	
	}	
	elsif (/(\d+) (\d+(\.\d+)?)/ && $pnum) {	
	    push( @{ $pt{$pnum} }, $1); # time
	    push( @{ $pc{$pnum} }, $2); # charge
	}
	elsif (/ekunden/) { # -H text
	    $text = $_;
	}    
    }
    # info
    $text = &fmt_date($text);
    print(p({-class=>'t'},$text));
    
    # sorting cheapest 1.
    sub bylast { $pc{$a}[$#{$pc{$a}}] <=> $pc{$b}[$#{$pc{$b}}] }
    sub byav {
	my ($v, $sa, $sb, $i);
	$i=0;
	foreach $v (0..$#{ $pc{$a} }) {
	    $sa += $pc{$a}[$v] * ($i>=8&&$i<18?5:2);
	    $i++;
	}    
	$i=0;
	foreach $v (0..$#{ $pc{$b} }) {
	    $sb += $pc{$b}[$v] * ($i>=8&&$i<18?5:2);
	    $i++;
	}    
	$sa <=> $sb;
    }
    my(@all, $sortfunc);
    my ($p, $max, $dy, $my, $min);
    $max=$n=0;
    $min = 99999;

    $sortfunc = $daily||$weekly ? \*byav : \*bylast;
    foreach $p (sort $sortfunc (keys(%pstring))) {
	# calc max
	foreach $i (0 .. $#{ $pc{$p} }) {
	    $max = $pc{$p}[$i] if ($pc{$p}[$i] > $max);
	    $min = $pc{$p}[$i] if ($pc{$p}[$i] < $min);
	}
	push(@all, $p);
	last if (++$n >= param('best'));
    }	    
    return unless($n);
    @all=reverse(@all);
    my ($font, $tx, $lw);
    $font = $swidth==1024 ? GD::gdMediumBoldFont : GD::gdSmallFont;
    $lw = int($DEP/$n);
    $DEP=$lw*$n;
    if (!param('_3D')) {
	$DEP=0;$lw=2;
	$LIN=50;
	$LEG+=50;
    }	
    # make img
    my $im = new GD::Image($W+$LEG+$DEP, $H+$DEP);	
    # alloc colors
    my(%rcols);
    my $c = 0;
    foreach $p (@all) {
	$c=del1_vbn($p);
	(undef, $r, $g, $b) = split(/-/, $rawcolors[$c]);
#	$pstring{$p} = "$r $g $b";
    	$rcols{$p} = $im->colorAllocate(eval($r), eval($g), eval($b));
    }	
    $white = $im->colorAllocate(255,255,255);
    $llgrey = $im->colorAllocate(0xf0,0xf0,0xf0);
    $im->transparent($llgrey);
    $lgrey = $im->colorAllocate(0xe0,0xe0,0xe0);
    $dgrey = $im->colorAllocate(0x80,0x80,0x80);
    $black = $im->colorAllocate(0,0,0);
    $borcol =$im->colorAllocate(0xff,0xff,0xe0);
    # all transparent
    $im->filledRectangle(0,0,$W+$LEG-1+$DEP,$H-1+$DEP,$llgrey);
    # drawing region
    my $poly = new GD::Polygon;
    $poly->addPt($DEP,0);
    $poly->addPt($xo,$DEP);
    $poly->addPt($xo,$DEP+$H);
    $poly->addPt($W,$DEP+$H);
    $poly->addPt($W+$DEP,$H);
    $poly->addPt($W+$DEP,0);
    $im->filledPolygon($poly, $white);
    # draw axis region
    my $poly = new GD::Polygon;
    $poly->addPt(0,$DEP+0);
    $poly->addPt(0,$DEP+$H-1);
    $poly->addPt($W-1,$DEP+$H-1);
    $poly->addPt($DEP+$W-1,$H-1);
    $poly->addPt($DEP+$W-1,$H-1-$yo);
    $poly->addPt($W-1,$DEP+$H-$yo);
    $poly->addPt($xo,$DEP+$H-$yo);
    $poly->addPt($xo,$DEP+0);
    $poly->addPt($DEP+$xo,0);
    $poly->addPt($DEP,0);
    # borders
    $im->rectangle($DEP+$xo,0,$DEP+$W-1,$H-$yo,$black);
    $im->line($W,$DEP,$W+$DEP,0,$black);
    $im->line($xo,$H+$DEP-$yo,$xo+$DEP,$H-$yo,$black);
    $im->filledPolygon($poly, $borcol);
    $im->polygon($poly, $black);
    $im->line($W-1, $DEP+$H-1, $W-1, $DEP+$H-$yo, $black); # last x-tick
    
    my ($x,$y);
    # y-scaling
    ($sy,$my,$dy) = _best_ends($min, $max,4..6);
    $dy = ($my-$sy)/$dy;
    $ys = ($H-$yo)/($my-$sy);
    # y-axis
    sub yaxis {
	my($fg) = $_[0];
	my($col) = $fg?$lgrey:$dgrey;
	for ($i = $sy; $i <= $my; $i+=$dy) {
	    $y = &_y($i);
    	    $im->line($i<$my&&$i?$xo/2:0, $y,$xo,$y,$black) if($fg); # tick
	    if($i<$my && $i>$sy) {
		if($fg) {
    		    $im->line($xo+1, $y,$W-1,$y,$col);
		}
		else {    
    		    $im->line($xo+1, $y,$xo+1+$DEP,$y-$DEP,$col);
    		    $im->line($DEP+$xo+1, $y-$DEP,$DEP+$W-1,$y-$DEP,$col);
		}    
	    }    
	    $im->string($font, 4, $y+2, $i, $black); # price
	}
	$im->string($font, 4, &_y($my)+3+$font->height, $cur, $black);
    } #yaxis
    	
    # x-scaling
    if ($weekly) {
	$len=7*24+1;
	$dx=1;
    }
    elsif ($daily) {
	$len=25;
	$dx=2;
    }	
    elsif ($mix) {	
	$dx=1;
    }
    else {	
	$dx = $len>240 ? 60 : $len>=120 ? 20 : 10;
    }	
    $xs = ($W-$xo)/($len-1);
    # x-axis
    sub xaxis {
	my($fg) = $_[0];
	my @days= qw(Mo Di Mi Do Fr Sa So);
	my($col) = $fg?$lgrey:$dgrey;
	for ($i = 0; $i < $len-1; $i+=$dx) {
	    if (($weekly && $i%8==0) || !$weekly) {
		$x = &_x($i); 
		$im->line($x, $DEP+$H-$yo/2, $x, $DEP+$H-$yo, $black); # tick
		if ($i) {
		    if($fg) {
			$im->line($x, $DEP+$H-$yo-1, $x, $DEP+1, $col);
		    }
		    else {	
			$im->line($DEP+$x, $H-$yo-1, $DEP+$x, 1, $col);
			$im->line($x, $DEP+$H-$yo-1, $DEP+$x, $H-1-$yo, $col);
		    }	
		}	
	    }    
	    next unless($fg);
	    if ($weekly) {
		$tx='';
		if ($i % 8==0) {
		    $tx = $i % 24 == 0 ? $days[$i/24] : $i % 24;
		}	
	    }
	    elsif ($daily) {
		$tx = $i;
	    }    
	    elsif ($mix) {
		if ($i == $len-2 && param('cost') > 0) {
		    $tx = 'Geb.';
		}
		else {     
		    $tx = $i+1;
		    $tx .= '>'. substr(param("tel$i"),0,4) if($len<=10);
		}    
	    }    
	    else {    
		$tx = $i == $dx*(int($len/$dx)-1) ? $i ." s" : $i; # nn s
	    }    
	    $im->string($font, &_x($i)+3, $DEP+$H-$yo+2, $tx, $black); 
	}	
    } # xaxis	
    # data	
    &yaxis(0);
    &xaxis(0);
#goto nodata;    
    my ($ii,$k,$x2,$y2,$col, $dep);
    $dep=$DEP-$lw if($DEP);
    foreach $p (@all) {
	foreach $i (0..$#{ $pc{$p} }-1) {
	    $ii=$i+1;
	    $x = &_x($pt{$p}[$i]);
	    $x2 = &_x($pt{$p}[$ii]);
	    $y = &_y($pc{$p}[$i]);
	    $y2 = &_y($pc{$p}[$ii]);
	    if ($i==0 && $y2 && $DEP) {
		for $k ($dep ..$dep+$lw-2) {
    		    $im->line($k+$x, -$k+$y-1, $k+$x,-$k+&_y($sy)-3, 
			$k==$dep ?$rcols{$p} :$lgrey);
		}    
	    }	
	    $im->filledRectangle($dep+$x,-$dep+$y,$dep+$x2,-$dep+&_y($sy),$lgrey) if(0);
	    for ($k=$dep+$lw-1;$k>=$dep;$k--) {
		$col = $k>$dep||!$DEP?$rcols{$p}:$black;
		if ($pt{$p}[$i]+1 == $pt{$p}[$ii] && !$daily && !$weekly && !$mix) {
    		    $im->line($k+$x, -$k+$y,$k+$x2, -$k+$y2, $col);
		}
		else {	
    		    $im->line($k+$x, -$k+$y, $k+$x2, -$k+$y,$col);
    		    $im->line($k+$x2,-$k+$y, $k+$x2, -$k+$y2,$col);
		}	
	    }	
	}
	if ($DEP) {
    	    for $k ($dep ..$dep+$lw-2) {
    		$im->line($k+$x2, -$k+$y2-1, $k+$x2,-$k+&_y($sy)-3, 
		    $k==$dep ?$rcols{$p} :$lgrey);
	    }	    
	}    
	$dep-=$lw if($DEP);
    }
nodata:    
    &yaxis(1);
    &xaxis(1);
#goto nolegend;    
    # legend
    my ($ndy, $sty, $mapx, $mapy, @map);
    $y=$H-$yo+$DEP;
    $i=min($n, $y/($font->height + 1));
    $dep=$lw/2;
    foreach $p (reverse(@all)) {
    	$ndy = ($font->height + 1)*$i;
	$sty = &_y($pc{$p}[$#{$pc{$p}}])-$dep;
	$y = max($ndy,min($y,$sty));
	$im->dashedLine($W+2+$dep,$sty, $W+$LIN+$DEP,$y, $rcols{$p});	
	$im->dashedLine($W+2+$dep+1,$sty-1, $W+$LIN+$DEP+1,$y-1, $rcols{$p});	
	$im->string($font, $mapx=$W+$LIN+5+$DEP, $mapy=$y-$font->height/2, "$p ".$pstring{$p}, $rcols{$p});
	&add_map($p, $mapx, $mapy, $font->height, $W+$LEG+$DEP, \@map);
	$y -= $font->height+1;
	$dep+=$lw if($DEP);
	$i--;
    }	
nolegend:    
    # front box lines
    $im->line($xo+1,$DEP,$W-1,$DEP,!$DEP?$black:$lgrey);
    $im->line($W-1,$DEP,$W-1,$DEP+$H-1-$yo,!$DEP?$black:$lgrey);
    $im->line($W,$DEP,$DEP+$W-1,1,$lgrey) if($DEP);
    $font = GD::gdSmallFont;
    $im->filledRectangle($DEP+$xo+5,10,
	$DEP+$xo+9+$font->width*length($LOGO),13+$font->height,$llgrey);
    $im->string($font,$DEP+$xo+7,12,$LOGO,$black);
    # write file
    $tempf = `$MKTEMP -q "$tempdir/irXXXXXX"`;
    chomp($tempf);
    rename($tempf, "$tempf.gif") || print(p,"Can't rename $tempf");;
    $tempf = "$tempf.gif";
    open(TEMP,">$tempf") || print(p,"Can't write $tempf");
    print(TEMP $im->gif);
    close(TEMP);
    # ret img tag
    $tempf =~ s!^$tempdir/!!;
    print(qq(<MAP NAME="map">\n));
    foreach (@map) {
	print("$_\n");
    }	
    print(qq(</MAP>\n));
    print(img({-src=>"$tempdir_url/$tempf", -height=>$H+$DEP, -border=>0,
    	-width=>$W+$LEG+$DEP, -align=>'"CENTER"', -usemap=>'#map'}),br);
    if ($mix) {
	print_ign($ign1, $ign2);
    }
}

sub add_map {
  my ($p, $mapx, $mapy, $height, $width, $mref) = @_;
  my($xu,$yu, $url);
  $mapx=int($mapx);
  $mapy=int($mapy);
  $xu=int($width-2);
  $yu=int($mapy+$height);
  $url=$url{$p};
  push(@$mref,qq(<AREA SHAPE="RECT" COORDS="$mapx,$mapy,$xu,$yu" href="$url"}>));
}      
sub _y {
    $DEP+$H-$yo-($_[0]-$sy)*$ys;
}
sub _x {
    $xo+$_[0]*$xs;
}   
 
# del gifs older then 1 hour
sub clean_up {
    my(@All, $file, $now);
    opendir(DIR, $tempdir);
    @All = readdir(DIR);
    closedir(DIR);
    $now=time();
    foreach $file (@All) {
	if($now - (stat("$tempdir/$file"))[9] > 3600 && $file =~ /^ir.{6}\.gif/) {
	    unlink("$tempdir/$file");
	}
    }
}

sub get_info {
    my ($pnum, $sect) = @_;
    my (@lines, $t);
    $pnum = del1_vbn($pnum);
    my @args=($ISDNRATE,"-p$pnum -X$sect $probe_zones[0]");
    _call_isdnrate(\@lines, @args);
    chomp(@lines);
    $t = (split(/  +/,$lines[0]))[3];
    shift(@lines);
    $t .= ' '. join(' ', @lines) if(@lines);
    $t;
}

sub del0 {
    $_[0] =~ s/(\d+\.\d{0,2}\d*?)0+/$1/;
    $_[0];
}    
# info: show info for provider
sub info {
    my($pnum) = $_[0];
    $info=1;
    my(@lines, $l, $prov, $cur, $charge, $rest, $day, $text, $fromgraf, $sav_q);
    my (@args);
    for $day ('W', 'N') { #
	param('day',$day);
	for $l (1,140) {
	    my (@one, $pp);
	    $pp = del1_vbn($pnum);
	    @args =("$ISDNRATE", "-HL -p$pp -l$l -d$day");
	    push(@args, "-f" . param('from')) if(param('from'));
	    push(@args, param('tel')) if(param('tel'));
	    push(@args, param('tel0')) if(!param('tel')); # coming from mix
	    _call_isdnrate(\@one, @args);
	    # -H & empty
	    $text = $one[0] unless($text);
	    print(pre(@one)) if($debug==2);
	    push(@lines, $one[2]);    
	}    
    }
    # restore q/Q
    
    # print report
    my (@mind,@unit, @dur, @mp, $i, $zone, $day, @time,$takt, $sales);
    $i=0;
    foreach (@lines) { #TODO
	(undef,$prov,$zone,$day,$time[$i],$cur,$charge,$mind[$i],
	    $unit[$i],$dur[$i],$mp[$i],$takt, $sales) = split_line($_);
	    $dur[$i]=int($dur[$i]);
	    $mp[$i]=round($mp[$i]);
	    $mind[$i]=del0($mind[$i]);
	print("$unit[$i], $dur[$i], $mp[$i], $zone") if($debug==2);
	$i++;
    }
    print(h2('Provider',$pnum,'-', $prov),h3('Tarife'));
    
    @time=qw(Tag Tag Abend Abend) unless $time[0];
    $text =~ s/^.*?indung //;
    $text =~ s/ kost.*//;  
    print("Bei einem Gespr�ch ($text) in der Zone '$zone' ",
	'sind die Tarifseinheiten ');
    my $any=0;
    if ($unit[1] != $unit[3]) {
        $any=1;
    }	    
    if ($any) {
	print("unterschiedlich teuer, am Werktag ($time[1]), ",
	    del0($unit[1]), " sonst ($time[3]) ", del0($unit[3])," $cur.");
    }
    else {		
	print('gleich teuer, und zwar ', del0($unit[1]), " $cur",'.'); 
    }	
    $any=0;
    for ($i = 1; $i <= $#dur; $i++) {
	if ($dur[0] != $dur[$i]) {
	    $any=1;
	    last;
	}
    }	    
    if (!$any) {
	print(br,"Die Impulsdauer ist einheitlich $dur[0]s.");
    }	
    elsif ($dur[1] != $dur[3]) {
	print(br,"Die Impulsdauer ist am Tag $dur[1]s, sonst $dur[3]s.");
    }	
    elsif ($dur[0] != $dur[1]) {
	print(br,"Der erste Taktimpuls dauert $dur[0]s, dann $dur[1]s, Takt $takt") if($dur[0]);   
	print(".");	
    }
    if($mind[0] > 0) {
	print(br,"Der Provider verrechnet eine Gespr�chs-Herstellungsgeb�hr von ",
	    "$mind[0] $cur.");
    }	    
    if($sales > 0) {
	print(br,"Der Provider verrechnet eine Mindestgespr�chsgeb�hr von ",
	    "$sales $cur.");
    }	    
    my $anytag=0;
    for ($i = 1; $i <= $#mp; $i++) {
	if ($mp[0] != $mp[$i]) {
	    $anytag=1;
	    last;
	}
    }	    
    my($t);
    $t="(theoretischen) " if((int($dur[1]) && 60 % $dur[1]) || (int($dur[3]) && 60 % $dur[3]));
    print(br,"Das f�hrt in der genannten Zone zu einem ${t}Minutenpreis ");
    if($anytag) {
	my($m1,$m2, $url);
	$m1="$mind[1] + " if($mind[1] > 0);
	$m2="$mind[3] + " if($mind[3] > 0);
	print("von am Tag $m1$mp[1], sonst $m2$mp[3] $cur.");
	$url = self_url;
	$url =~ s/&_3D=\w+//;
	$url =~ s/&tab=\w+//;
	$url =~ s/&now=\w+//;
	$url =~ s/&mix=\w+//;
	$url =~ s/&graf=\w+//;
	$url =~ s/&info=\w+//;
	$url =~ s/&d?day=[\w.]+//g;
	$url =~ s/&hour=[\w:]+//;
	$url =~ s/&\w+=&/&/g;
	$url .= "&prov=$pnum&graf=Tag&dday=W";
	$url .= "&tel=".param('tel0') if(!param('tel')); # coming from mix
	print(p,'F�r eine genauere G�ltigkeit der Preise w�hlen ',
	    'Sie bitte die ',a({-href=>$url},'Tages�bersicht'),'.');
    }
    else {	
	my($m1);
	$m1="$mind[3] + " if($mind[3] > 0);
	print("von einheitlich $m1$mp[3] $cur.");
    }
    
    # probzones
    print(p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),
	h3('Inlands-Zonen (Festnetz)'));
    @lines = ();
    my ($j,%mp);
    undef $text;
    for $day ('W', 'N') { #
	for ($j=0; $j < @probe_zones; $j++) {
	    for ($i=$j+1; $i < @probe_zones; $i++) {
		my(@one, $pp);
		$pp = del1_vbn($pnum);
		@args =("$ISDNRATE", "-HL -p$pp -l60 -d$day");
		push(@args, "-f" . $probe_zones[$j]);
		push(@args,  $probe_zones[$i]);
		_call_isdnrate(\@one, @args);
	# -H & empty
		$text = $one[0] unless($text);
    		(undef, $prov, $zone,undef,undef,$cur, $charge) = &split_line($one[2]);
		$mp{qq("$zone"$day)}= &round($charge);
		push(@lines, qq("$zone"));    
	    }    
	}
    }
    $any=0;
    my %uniqz;
    $uniqz{$lines[0]}=0;
    for ($i = 1; $i <= $#lines/2; $i++) {
	if ($lines[0] ne $lines[$i]) {
	    $any=1;
	    $uniqz{$lines[$i]}=$i;
	}
    }	    
    if ($any) {
	print("Der Provider $pnum hat im Inland unterschiedliche Zonen ",
	    "n�mlich ", join(', ',sort{$uniqz{$a}<=>$uniqz{$b}}(keys(%uniqz))),'.');
    }
    else {	    
	print("Der Provider $pnum hat im Inland ein einheitliches Zonenschema.");
    }	
    $text =~ s/g von.*/g/;
    print(br,$text, "kostet:",br,p);
    my $t = q!print(blockquote(table({-border=>1,-cellspacing=>0,-cellpadding=>4},
	Tr(th({-rowspan=>2},'Zone'),th({-colspan=>2},$cur)),
	Tr(th(['Tag','Nacht'])),!;
    foreach $i (sort{$uniqz{$a}<=>$uniqz{$b}} (keys(%uniqz))) {
	$t .= q!Tr(td([! . $i . ',"' . $mp{"${i}W"} .'","'. $mp{"${i}N"} . q!"])),!; #"
    }	
    $t .= ')));';
    eval($t);
    if ((int($dur[1]) && 60 % $dur[1]) || (int($dur[3]) && 60 % $dur[3])) {
	print('Hinweis: Durch die Taktung sind die echten Minutenpreise ',
	    'eventuell h�her, als die theoretischen oben angef�hrten.',p);
    }	    
    print(p({-class=>'sm'},'Tag: Werktag 10<sup>00</sup>, Nacht: Werktag 23<sup>00</sup>. ',
	'Der Provider hat eventuell eine weitere Unterteilung der Tarife ',
	'pro Tageszeit/Wochentag. Genauers dazu sehen sie unter den Schaltfl�chen ',
	i('Tag'),' (Tagesverlauf) und ',i('Woche'),' (Wochenverlauf).')) if($anytag);
    print ($t=get_info($pnum, 'Zone')) if($t ne '');
    
    print(p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),h3('Geb�hren'));
    print (get_info($pnum, 'GT')||'Keine');
    print(p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),h3('Besonderheiten'));
    print (get_info($pnum, 'Special')||'Keine');
    print(p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),h3('Kontakt'));
    print(table({-cellpadding=>3},Tr([
	td(['Name',get_info($pnum, 'Name')]),
	td(['Adresse',get_info($pnum, 'Address')]),
	td(['Homepage &nbsp;',a({-href=>($t=get_info($pnum, 'Homepage')),-target=>'blank'},$t)]),
	td(['Tarife &nbsp;',a({-href=>($t=get_info($pnum, 'TarifURL')),-target=>'blank'},$t)]),
	td(['E-Mail',a({-href=>'mailto:'.($t=get_info($pnum, 'EMail'))},$t)]),
	td(['Hotline',get_info($pnum, 'Hotline')]),
	td(['Telefon',get_info($pnum, 'Telefon')]),
	td(['Telefax',get_info($pnum, 'Telefax')])
	])));

    my $url = self_url;
    $url =~ s/&info=\w+//;	
    print(p,img({-src=>"/telrate/hr.gif", -width=>600, -height=>4, -alt=>'-----'}),p 
	a({-href=>'javascript:history.back()'},'[ Zur�ck ]'),
	a({-href=>$url},'[ Vorhergehende Seite ]'));
    footer($pnum);	
}    

sub list {
    my ($what)=$_[0];
    my (@args, %prov, %hp, %tarif, %tel, %fax);
    my (@X, @var, $i, $t, $pnum, $prov);
    @X = qw( Homepage TarifURL Telefon Hotline Telefax );
    @var = ( \%hp, \%tarif, \%tel, \%tel, \%fax );
    for ($i = 0; $i < @X ; $i++) {
	my (@args, @lines);
	push(@args, $ISDNRATE,"-Sv $probe_zones[0]");
	push(@args, "-X$X[$i]");
	_call_isdnrate(\@lines, @args);
	chomp(@lines);
	foreach (@lines) {
	    ($prov, undef, undef, $t) = split(/  +/);
	    ($pnum, $prov) = split(/:/, $prov);
	    $prov{$pnum} = $prov unless $prov{$pnum};
	    $var[$i]->{$pnum} = $t unless $var[$i]->{$pnum};
	}
    }	    
    my (@th);
    @th = qw ( Nr. Provider/Homepage Tarife Telefon Fax );
    print('<table cellspacing=3 cellpadding=3>',Tr(th([@th])));
    $i = 0;	
    foreach $pnum (sort (keys(%prov))) {
	my $hp = $hp{$pnum};
	$prov = $prov{$pnum};
	my $bgcolor=++$i&1?' bgcolor="#fffff0"':'';
	print("<tr$bgcolor><td>$pnum</td>",   
	    "<td><a href=\"$hp\">$prov</a></td>");
	my $tu = $tarif{$pnum};
	($t= $tu) =~ s#\Q$hp\E##;
	my $tel = $tel{$pnum};
	$tel =~ s/,\s*/<br>/;
	my $fax = $fax{$pnum};
	$fax =~ s/,\s*/<br>/;
	print("<td><a href=\"$tu\">$t</a></td><td>" .
	    "$tel</td><td>$fax</tr>");
    }
    print('</table><p><br>');
    $help=3;
    footer($pnum);	
}
    	    
# next is from GIFgraph

# Usage:
#	($nmin,$nmax,$nint) = _best_ends(247, 508);
#	($nmin,$nmax) = _best_ends(247, 508, 5); 
#		use 5 intervals
#	($nmin,$nmax,$nint) = _best_ends(247, 508, 4..7);	
#		best of 4,5,6,7 intervals

sub _best_ends {
	my ($min, $max, @n) = @_;
	my ($best_min, $best_max, $best_num) = ($min, $max, 1);

	# fix endpoints, fix intervals, set defaults
	($min, $max) = ($max, $min) if ($min > $max);
	($min, $max) = ($min) ? ($min * 0.5, $min * 1.5) : (-1,1) 
	if ($max == $min);
	@n = (3..6) if (@n <= 0 || $n[0] =~ /auto/i);
	my $best_fit = 1e30;
	my $range = $max - $min;

	# create array of interval sizes
	my $s = 1;
	while ($s < $range) { $s *= 10 }
	while ($s > $range) { $s /= 10 }
	my @step = map {$_ * $s} (0.2, 0.5, 1, 2, 5);

	for (@n) 
		{								
		# Try all numbers of intervals
		my $n = $_;
		next if ($n < 1);
		for (@step) 
		{
			next if ($n != 1) && ($_ < $range/$n); # $step too small

			my $nice_min   = $_ * int($min/$_);
			$nice_min  -= $_ if ($nice_min > $min);
			my $nice_max   = ($n == 1) 
				? $_ * int($max/$_ + 1) 
				: $nice_min + $n * $_;
			my $nice_range = $nice_max - $nice_min;

			next if ($nice_max < $max);	# $nice_min too small
			next if ($best_fit <= $nice_range - $range); # not closer fit

			$best_min = $nice_min;
			$best_max = $nice_max;
			$best_fit = $nice_range - $range;
			$best_num = $n;
		}
	}
	return ($best_min, $best_max, $best_num)
}
sub read_countries {
    my @allcountries=($LEER, "Afghanistan","�gypten","Alaska","Albanien","Algerien",
"Amerikanisch-Samoa","Amerikanische Jungferninseln","Andorra",
"Angola","Anguilla","Antarktis","Antigua und Barbuda","�quatorial-Guinea",
"Argentinien","Armenien","Aruba","Ascension",
"Aserbaidschan","�thiopien","Atlantischer Ozean (Ost)",
"Atlantischer Ozean (West)","Australien","Azoren",
"Bahamas","Bahrain","Bangladesch","Barbados","Belgien",
"Belize","Benin","Bermuda","Bhutan","Bilbao","Bolivien","Bosnien-Herzegowina",
"Botsuana Botswana","Brasilien","Britische Jungferninseln",
"Brunei","Bulgarien","Burkina Faso Obervolta","Burundi","Cape Verde",
"Chatham-Inseln","Chile","China","Cookinseln","Costa Rica",
"Deutschland","Diego Garcia","Dominica","Dominikanische Republik","Dschibuti",
"D�nemark","Ecuador","Edinburgh",
"El Salvador","Elfenbeink�ste Cote de Ivoire",
"Eritrea","Estland","Falklandinseln","Fidschi","Finnland","Frankfurt",
"Frankreich","Franz�sisch-Guayana","Franz�sisch-Polynesien",
"Freephone Niederlande","Freephone Schweiz","F�r�er-Inseln","Gabun",
"Gambia","Georgien","Gerona","Ghana","Gibraltar","Grenada","Griechenland",
"Gro�britannien","Gr�nland","Guadeloupe","Guam",
"Guantanamo","Guantanamo Bay","Guatemala","Guinea","Guinea-Bissau","Guyana",
"Haiti","Hawaii","Honduras","Hongkong","Indien",
"Indischer Ozean","Indonesien","Inmarsat A","Inmarsat A Daten/Fax",
"Inmarsat Aero","Inmarsat B","Inmarsat B HSD","Inmarsat M","Inmarsat Mini-M",
"Irak","Iran","Iridium 008816","Iridium 008817","Irland",
"Island","Israel","Italien","Jamaika",
"Japan","Jemen (Arab. Republik)","Jordanien","Jugoslawien",
"Kaimaninseln","Kambodscha","Kamerun","Kanada","Kanarische Inseln","Kasachstan",
"Katar","Kenia","Kirgisien","Kirgistan","Kiribati","Kokosinseln","Kolumbien",
"Kongo","Korea Rep. (South)","Kroatien","Kuba",
"Kuwait","Laos","Lesotho","Lettland","Leuven","Libanon",
"Liberia","Libyen","Liechtenstein","Litauen","Luxemburg","Macao","Madagaskar",
"Madeira","Malawi","Malaysia","Malediven","Mali","Mallorca","Malta",
"Marianen (SaipanNord-)","Marokko","Marshallinseln","Martinique / Franz. Antillen",
"Mauretanien","Mauritius","Mayotte","Mazedonien","Mexiko","Midway-Inseln",
"Mikronesien","Moldavien Moldau (Republik)","Monaco","Mongolei","Montserrat",
"Mosambik","Myanmar Burma","Namibia","Nauru","Nepal","Neukaledonien",
"Neuseeland","Nicaragua","Niederl. Antillen","Niederlande",
"Niger","Nigeria","Niue-Inseln","Nordirland","Nordkorea","Norfolkinseln",
"Norwegen","Oman","�sterreich","Pakistan","Palau /Belau","Panama",
"Papua-Neuguinea","Paraguay","Pazifischer Ozean",
"Peru","Philippinen","Pitcairn Inseln","Pitcairn Islands",
"Polen","Portugal","Puerto Rico","Reunion","Rotterdam","Ruanda","Rum�nien",
"Russische F�derat. (westl.)","Russische F�rderation (�stl.)",
"Salomonen","Sambia","San Marino","Sao Tome und Principe","Saudi Arabien",
"Schweden","Schweiz","Senegal",
"Seyschellen","Sierra Leone","Simbabwe","Singapur","Slowakische Republik",
"Slowenien","Somalia","Spanien","Sri Lanka","St. Helena",
"St. Kitts und Nevis","St. Lucia","St. Pierre und Miquelon","St. Vincent und Grenadinen",
"Sudan","Suriname","Swasiland","Syrien","S�dafrika","S�dkorea",
"Tadschikistan","Taiwan","Tansania","Tarragona","Teneriffa",
"Thailand","Togo","Tokelan","Tokyo","Tonga","Trinidad und Tobago",
"Tristan da Cunha","Tschad","Tschechische Republik","Tunesien","Turkmenistan",
"Turks- und Caicosinseln","Tuvalu","T�rkei","USA",
"Uganda","Ukraine","Ungarn","Uruguay","Usbekistan","Valencia","Vanuatu",
"Vatikan","Venezuela","Ver. Arabische Emirate",
"Vietnam","Wake Inseln","Wallis und Futuna","Weihnachtsinseln","Weissrussland",
"West-Samoa","Zaire","Zentralafrikanische Republik","Zypern");
   my($c, $wanted);	
   @countries = ( $LEER );
   if (param('country') && ($wanted=substr(param('country'),0,1)) ne '-') {
       foreach (@allcountries) {
           push(@countries, $_) if(/^$wanted/i);
       }
    }
    for $c ('A'..'Z','�','�','�') {
	push(@countries, $c);
    }
    @countries = sort byname (@countries);
}   