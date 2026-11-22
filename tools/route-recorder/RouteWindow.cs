using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
class RouteWindow : Form {
 [DllImport("user32.dll")] static extern bool ReleaseCapture(); [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h,int m,IntPtr w,IntPtr l);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr OpenEvent(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] static extern bool SetEvent(IntPtr handle);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 readonly string root=AppDomain.CurrentDomain.BaseDirectory;
 readonly TextBox journal=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical}; readonly ListBox files=new ListBox();readonly Label status=new Label();
 readonly Button record=new StudioButton(),approach=new StudioButton(),replay=new StudioButton(),stop=new StudioButton();
 Process worker;bool closeAfterStop=false;string lastOutput="";
 RouteWindow(){
  Text="Пародия — маршруты CS2";ClientSize=new Size(760,640);MinimumSize=new Size(700,600);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",10);
  var hint=new Label{Text="1. Запись → вернись в CS2 → отпусти клавиши на секунду → звуковой сигнал.\n2. Пройди маршрут → F10. Переключение окна также завершает запись.\n3. Выбери запись → Подойти к старту → Воспроизвести.",Location=new Point(18,15),Size=new Size(624,72)};
  var note=new Label{Text="Пробный режим: одна CS2, локальная Mirage с ботами.\nПовтор требует активной CS2 и тех же биндов / чувствительности.",Location=new Point(18,91),Size=new Size(624,46)};
  record.Text="Записать";approach.Text="Подойти к старту";replay.Text="Воспроизвести";stop.Text="Стоп (F10)";
  Button[] buttons={record,approach,replay,stop};for(int i=0;i<buttons.Length;i++){buttons[i].Location=new Point(18+i*158,147);buttons[i].Size=new Size(150,36);Controls.Add(buttons[i]);}
  files.Location=new Point(18,198);files.Size=new Size(624,165);files.SelectedIndexChanged+=(s,e)=>UpdateButtons();
  var refresh=new StudioButton{Text="Обновить список",Location=new Point(18,372),Size=new Size(155,32)};refresh.Click+=(s,e)=>RefreshFiles();
  var folder=new StudioButton{Text="Папка записей",Location=new Point(185,372),Size=new Size(145,32)};folder.Click+=(s,e)=>{Directory.CreateDirectory(Path.Combine(root,"recordings"));Process.Start("explorer.exe","\""+Path.Combine(root,"recordings")+"\"");};
  status.Location=new Point(18,414);status.Size=new Size(624,48);status.Text="Готов. Старые записи без событий не подходят для точного повтора.";
  journal.Location=new Point(18,470);journal.Size=new Size(724,150);journal.Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top|AnchorStyles.Bottom;
  var validate=new StudioButton{Text="Проверить файл",Location=new Point(345,372),Size=new Size(150,32)};validate.Click+=(s,e)=>RunRoute("--validate");
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
  FormBorderStyle=FormBorderStyle.None;MouseDown+=(s,e)=>{if(e.Button==MouseButtons.Left){ReleaseCapture();SendMessage(Handle,0xA1,(IntPtr)2,IntPtr.Zero);}};
  ClientSize=new Size(1120,790);MinimumSize=Size;MaximumSize=Size;DoubleBuffered=true;
  BackColor=Color.FromArgb(9,10,10);ForeColor=Color.FromArgb(235,230,217);
  var oldLabels=new System.Collections.Generic.List<Control>();
  foreach(Control c in Controls){if(c is Label&&c!=status)oldLabels.Add(c);}
  foreach(Control c in oldLabels){Controls.Remove(c);c.Dispose();}
  var art=new ApolloViewport(Path.Combine(root,"art","phoenix","phoenix.scene"),()=>Form.ActiveForm==this&&WindowState!=FormWindowState.Minimized&&worker==null){Location=new Point(600,0),Size=new Size(520,790)};Controls.Add(art);art.SendToBack();
  foreach(Control c in Controls){var b=c as Button;if(b!=null){b.FlatStyle=FlatStyle.Flat;b.FlatAppearance.BorderColor=Color.FromArgb(53,51,44);b.BackColor=Color.FromArgb(19,20,19);b.ForeColor=Color.FromArgb(218,214,202);b.Cursor=Cursors.Hand;b.Font=new Font("Segoe UI",10);b.FlatAppearance.MouseOverBackColor=Color.FromArgb(45,43,36);}}
  record.SetBounds(48,238,250,46);record.Text="●   Начать запись";record.BackColor=Color.FromArgb(218,204,166);record.ForeColor=Color.FromArgb(17,18,16);record.FlatAppearance.BorderSize=0;
  stop.SetBounds(314,238,250,46);stop.Text="■   Остановить";
  approach.SetBounds(48,299,250,39);replay.SetBounds(314,299,250,39);
  files.SetBounds(48,389,516,145);files.BackColor=Color.FromArgb(16,17,16);files.ForeColor=Color.FromArgb(215,211,199);files.BorderStyle=BorderStyle.None;files.Font=new Font("Segoe UI",10);files.Anchor=AnchorStyles.Top|AnchorStyles.Left;
  foreach(Control c in Controls){var b=c as Button;if(b==null)continue;if(b.Text=="Обновить список")b.SetBounds(48,550,160,32);if(b.Text=="Папка записей")b.SetBounds(226,550,160,32);if(b.Text=="Проверить файл")b.SetBounds(404,550,160,32);}
  status.SetBounds(48,601,516,48);status.Padding=new Padding(10);status.BackColor=Color.FromArgb(22,23,20);status.ForeColor=Color.FromArgb(195,183,148);
  journal.SetBounds(48,676,516,78);journal.Anchor=AnchorStyles.Left|AnchorStyles.Top;journal.BackColor=Color.FromArgb(12,13,12);journal.ForeColor=Color.FromArgb(134,139,130);journal.BorderStyle=BorderStyle.None;journal.Font=new Font("Consolas",8);
  LabelText("P A R O D Y   /   R O U T E   S T U D I O",48,32,520,25,"Segoe UI",9,Color.FromArgb(174,161,124));
  LabelText("Искусство движения.",48,77,550,73,"Georgia",32,Color.FromArgb(239,235,222));
  LabelText("Запиши свой путь. Сохрани каждое действие.\nВернись к началу и проверь повтор.",50,168,510,48,"Segoe UI",11,Color.FromArgb(149,153,142));
  LabelText("01  /  СОХРАНЁННЫЕ МАРШРУТЫ",48,356,520,25,"Segoe UI",9,Color.FromArgb(154,146,123));
  LabelText("02  /  ЖУРНАЛ СЕССИИ",48,651,520,22,"Segoe UI",8,Color.FromArgb(128,129,116));
  LabelText("MIRAGE  ·  LOCAL TEST  ·  F10 STOP",48,765,520,20,"Segoe UI",8,Color.FromArgb(109,114,104));
  var close=new StudioButton{Text="×",Location=new Point(1064,20),Size=new Size(36,32),BackColor=Color.FromArgb(20,21,19),ForeColor=Color.FromArgb(210,205,188)};close.Click+=(s,e)=>Close();Controls.Add(close);close.BringToFront();
  Opacity=.86;var fade=new Timer{Interval=16};fade.Tick+=(s,e)=>{Opacity=Math.Min(1,Opacity+.014);if(Opacity>=1){fade.Stop();fade.Dispose();}};Shown+=(s,e)=>fade.Start();FormClosed+=(s,e)=>fade.Dispose();
 }
 void LabelText(string text,int x,int y,int w,int h,string font,float size,Color color){Controls.Add(new Label{Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=new Font(font,size),ForeColor=color,BackColor=Color.Transparent});}
 class StudioButton:Button{
  readonly Timer animation=new Timer{Interval=16};float blend=0,target=0;
  public StudioButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);animation.Tick+=(s,e)=>{blend+=(target-blend)*.28f;if(Math.Abs(target-blend)<.015f){blend=target;animation.Stop();}Invalidate();};}
  protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);target=1;animation.Start();}
  protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);target=0;animation.Start();}
  protected override void OnPaint(PaintEventArgs e){Color b=BackColor;int n=(int)(blend*14);Color fill=Color.FromArgb(Math.Min(255,b.R+n),Math.Min(255,b.G+n),Math.Min(255,b.B+n));using(var brush=new SolidBrush(fill))e.Graphics.FillRectangle(brush,ClientRectangle);using(var pen=new Pen(Color.FromArgb(54,53,46)))e.Graphics.DrawRectangle(pen,0,0,Width-1,Height-1);TextRenderer.DrawText(e.Graphics,Text,Font,ClientRectangle,Enabled?ForeColor:Color.FromArgb(102,105,96),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);}
  protected override void Dispose(bool disposing){if(disposing)animation.Dispose();base.Dispose(disposing);}
 }
 class StatuePanel:Panel{
  Image art;
  public StatuePanel(string path){DoubleBuffered=true;BackColor=Color.FromArgb(9,10,10);if(File.Exists(path))art=Image.FromFile(path);}
  protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);if(art!=null){e.Graphics.InterpolationMode=System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;e.Graphics.DrawImage(art,new Rectangle(24,70,470,720));using(var shade=new SolidBrush(Color.FromArgb(90,5,7,7)))e.Graphics.FillRectangle(shade,ClientRectangle);using(var gradient=new System.Drawing.Drawing2D.LinearGradientBrush(ClientRectangle,Color.FromArgb(255,9,10,10),Color.FromArgb(0,9,10,10),0f))e.Graphics.FillRectangle(gradient,ClientRectangle);using(var bottom=new System.Drawing.Drawing2D.LinearGradientBrush(new Rectangle(0,580,Width,210),Color.Transparent,Color.FromArgb(255,9,10,10),90f))e.Graphics.FillRectangle(bottom,0,580,Width,210);}
   using(var font=new Font("Georgia",20))using(var brush=new SolidBrush(Color.FromArgb(211,202,177)))e.Graphics.DrawString("APOLLO",font,brush,220,691);
   using(var font=new Font("Segoe UI",8))using(var brush=new SolidBrush(Color.FromArgb(151,149,131)))e.Graphics.DrawString("THE MET COLLECTION  /  PUBLIC DOMAIN",font,brush,158,731);
  }
  protected override void Dispose(bool disposing){if(disposing&&art!=null)art.Dispose();base.Dispose(disposing);}
 }
 [STAThread] static void Main(string[] args){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);if(args.Length==2&&args[0]=="--preview"){using(var form=new RouteWindow()){form.Show();form.Opacity=1;Application.DoEvents();using(var bmp=new Bitmap(form.ClientSize.Width,form.ClientSize.Height)){form.DrawToBitmap(bmp,new Rectangle(Point.Empty,form.ClientSize));bmp.Save(args[1]);foreach(Control c in form.Controls){var model=c as ApolloViewport;if(model!=null)model.SavePreview(args[1]+".model.png");}}}return;}Application.Run(new RouteWindow());}
}








