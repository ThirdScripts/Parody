using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
class RouteWindow : Form {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr OpenEvent(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] static extern bool SetEvent(IntPtr handle);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 readonly string root=AppDomain.CurrentDomain.BaseDirectory;
 readonly TextBox journal=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical}; readonly ListBox files=new ListBox();readonly Label status=new Label();
 readonly Button record=new Button(),approach=new Button(),replay=new Button(),stop=new Button();
 Process worker;bool closeAfterStop=false;string lastOutput="";
 RouteWindow(){
  Text="Пародия — маршруты CS2";ClientSize=new Size(760,640);MinimumSize=new Size(700,600);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",10);
  var hint=new Label{Text="1. Запись → вернись в CS2 → отпусти клавиши на секунду → звуковой сигнал.\n2. Пройди маршрут → F10. Переключение окна также завершает запись.\n3. Выбери запись → Подойти к старту → Воспроизвести.",Location=new Point(18,15),Size=new Size(624,72)};
  var note=new Label{Text="Пробный режим: одна CS2, локальная Mirage с ботами.\nПовтор требует активной CS2 и тех же биндов / чувствительности.",Location=new Point(18,91),Size=new Size(624,46)};
  record.Text="Записать";approach.Text="Подойти к старту";replay.Text="Воспроизвести";stop.Text="Стоп (F10)";
  Button[] buttons={record,approach,replay,stop};for(int i=0;i<buttons.Length;i++){buttons[i].Location=new Point(18+i*158,147);buttons[i].Size=new Size(150,36);Controls.Add(buttons[i]);}
  files.Location=new Point(18,198);files.Size=new Size(624,165);files.SelectedIndexChanged+=(s,e)=>UpdateButtons();
  var refresh=new Button{Text="Обновить список",Location=new Point(18,372),Size=new Size(155,32)};refresh.Click+=(s,e)=>RefreshFiles();
  var folder=new Button{Text="Папка записей",Location=new Point(185,372),Size=new Size(145,32)};folder.Click+=(s,e)=>{Directory.CreateDirectory(Path.Combine(root,"recordings"));Process.Start("explorer.exe","\""+Path.Combine(root,"recordings")+"\"");};
  status.Location=new Point(18,414);status.Size=new Size(624,48);status.Text="Готов. Старые записи без событий не подходят для точного повтора.";
  journal.Location=new Point(18,470);journal.Size=new Size(724,150);journal.Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top|AnchorStyles.Bottom;
  var validate=new Button{Text="Проверить файл",Location=new Point(345,372),Size=new Size(150,32)};validate.Click+=(s,e)=>RunRoute("--validate");
  Controls.AddRange(new Control[]{hint,note,files,refresh,folder,status,journal,validate});
  record.Click+=(s,e)=>Launch("parody.exe","");
  approach.Click+=(s,e)=>RunRoute("--approach");replay.Click+=(s,e)=>RunRoute("--replay");stop.Click+=(s,e)=>StopWorker();
  var timer=new Timer{Interval=250};timer.Tick+=(s,e)=>{
   if(worker!=null&&worker.HasExited){worker.WaitForExit();int code=worker.ExitCode;worker.Dispose();worker=null;status.Text=code==0?"Готово. "+lastOutput:"Остановлено: "+lastOutput;RefreshFiles();if(closeAfterStop)Close();}UpdateButtons();
  };timer.Start();
  FormClosing+=(s,e)=>{if(worker!=null&&!worker.HasExited){e.Cancel=true;closeAfterStop=true;StopWorker();}};
  FormClosed+=(s,e)=>timer.Dispose();RefreshFiles();ApplyDesign();
 }
 void RunRoute(string mode){if(files.SelectedItem==null)return;Launch("route_replay.exe",mode+" \""+Path.Combine(root,"recordings",files.SelectedItem.ToString())+"\" de_mirage");}
 void Launch(string executable,string args){
  if(worker!=null)return;
  try{lastOutput="";var p=new Process();p.StartInfo=new ProcessStartInfo(Path.Combine(root,executable),args){WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
   DataReceivedEventHandler output=(s,e)=>{if(e.Data==null||IsDisposed||!IsHandleCreated)return;try{BeginInvoke((Action)(()=>{lastOutput=e.Data;status.Text=e.Data;journal.AppendText(e.Data+Environment.NewLine);if(journal.TextLength>50000)journal.Text=journal.Text.Substring(journal.TextLength-30000);}));}catch(InvalidOperationException){}};
   p.OutputDataReceived+=output;p.ErrorDataReceived+=output;
   if(!p.Start()){p.Dispose();throw new Exception("Не удалось запустить процесс");}worker=p;p.BeginOutputReadLine();p.BeginErrorReadLine();status.Text=executable=="parody.exe"?"Вернись в CS2, отпусти клавиши на секунду. Начало — автоматически со звуком. Завершение — F10.":"Проверяю запись и стартовую позицию…";UpdateButtons();
  }catch(Exception ex){status.Text=ex.Message;}
 }
 void StopWorker(){if(worker==null||worker.HasExited)return;IntPtr h=OpenEvent(2,false,"Local\\CS2RouteStop-"+worker.Id);if(h!=IntPtr.Zero){SetEvent(h);CloseHandle(h);status.Text="Останавливаю и отпускаю кнопки…";}else status.Text="Процесс ещё запускается. Нажми Стоп повторно или F10.";}
 void UpdateButtons(){bool busy=worker!=null;record.Enabled=!busy;approach.Enabled=replay.Enabled=!busy&&files.SelectedItem!=null;stop.Enabled=busy;}
 void RefreshFiles(){string selected=files.SelectedItem as string;files.Items.Clear();string dir=Path.Combine(root,"recordings");if(Directory.Exists(dir)){var names=Directory.GetFiles(dir,"*.jsonl");Array.Sort(names);Array.Reverse(names);foreach(var n in names)files.Items.Add(Path.GetFileName(n));}if(selected!=null&&files.Items.Contains(selected))files.SelectedItem=selected;else if(files.Items.Count>0)files.SelectedIndex=0;UpdateButtons();}
 void ApplyDesign(){
  ClientSize=new Size(800,720);MinimumSize=new Size(800,720);
  BackColor=Color.FromArgb(16,18,25);ForeColor=Color.FromArgb(230,233,243);Font=new Font("Segoe UI",10);
  foreach(Control c in Controls){c.Top+=65;c.ForeColor=Color.FromArgb(165,174,194);
   var b=c as Button;if(b!=null){b.FlatStyle=FlatStyle.Flat;b.FlatAppearance.BorderColor=Color.FromArgb(52,58,76);b.BackColor=Color.FromArgb(32,36,49);b.ForeColor=Color.FromArgb(235,237,246);b.Cursor=Cursors.Hand;b.FlatAppearance.MouseOverBackColor=Color.FromArgb(53,48,78);}
  }
  record.BackColor=Color.FromArgb(112,80,224);record.FlatAppearance.BorderSize=0;record.ForeColor=Color.White;
  stop.BackColor=Color.FromArgb(70,35,47);stop.ForeColor=Color.FromArgb(255,184,191);
  files.BackColor=Color.FromArgb(24,28,39);files.ForeColor=Color.FromArgb(225,230,245);files.BorderStyle=BorderStyle.None;files.ItemHeight=25;files.Size=new Size(760,165);files.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;
  journal.BackColor=Color.FromArgb(11,14,20);journal.ForeColor=Color.FromArgb(167,184,209);journal.BorderStyle=BorderStyle.FixedSingle;journal.Font=new Font("Consolas",9);journal.Size=new Size(760,150);
  status.ForeColor=Color.FromArgb(208,198,255);status.BackColor=Color.FromArgb(27,25,40);status.Padding=new Padding(8);status.Width=760;
  Controls.Add(new Label{Text="PARODY",Font=new Font("Segoe UI Semibold",23),ForeColor=Color.White,Location=new Point(18,15),Size=new Size(220,45)});
  Controls.Add(new Label{Text="ROUTE STUDIO  /  экспериментальная версия",Font=new Font("Segoe UI",9),ForeColor=Color.FromArgb(146,130,211),Location=new Point(235,31),Size=new Size(510,25)});
 }
 [STAThread] static void Main(){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new RouteWindow());}
}


